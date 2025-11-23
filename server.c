/*
Compile:
gcc -O2 -Wall server_simple.c -o server -lmicrohttpd -lpq -pthread

Usage:
./server <port> <mode>
mode = 0 (DB-only), 1 (Cache-only), 2 (Hybrid)
*/

#include <microhttpd.h>
#include <postgresql/libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include "uthash.h"

#define MAX_PLAYERS 10000
#define DEFAULT_PORT 8080
#define DEFAULT_TOP 10

#define MAX_CACHE_SIZE 1000
#define POOL_SIZE 90

typedef struct LRUNode
{
    int id;
    int score;
    struct LRUNode *prev, *next;
    UT_hash_handle hh; /* makes this struct hashable by uthash */
} LRUNode;

static PGconn *pool[POOL_SIZE];
static int pool_busy[POOL_SIZE];
pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pool_wait = PTHREAD_COND_INITIALIZER;

static LRUNode *head = NULL, *tail = NULL;
static int cache_count = 0;
static LRUNode *cache_map = NULL; /* uthash hash table root (player_id -> node) */
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    int id;
    int score;
} Player;

static int mode = 0; // 0=DB, 1=Cache, 2=Hybrid

long long now_us()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

// ---------- DB Section ----------

PGconn *create_new_connection()
{
    PGconn *c = PQconnectdb("host=127.0.0.1 port=5432 dbname=leaderboard_db user=leaderboard_user password=leaderboard_pw");

    if (!c)
    {
        fprintf(stderr, "PQconnectdb returned NULL\n");
        return NULL;
    }

    if (PQstatus(c) != CONNECTION_OK)
    {
        fprintf(stderr, "Connection failed: %s\n", PQerrorMessage(c));
        PQfinish(c);
        return NULL;
    }
    printf("Connected to DB: %s\n", PQdb(c));
    printf("User: %s\n", PQuser(c));
    printf("Host: %s\n", PQhost(c));
    printf("Port: %s\n", PQport(c));
    return c;
}

void pool_init()
{
    for (int i = 0; i < POOL_SIZE; i++)
    {
        pool[i] = create_new_connection();
        if (!pool[i])
        {
            fprintf(stderr, "Failed to initialize DB pool at index %d\n", i);
            // Clean up any connections created so far.
            for (int j = 0; j < i; j++)
            {
                if (pool[j])
                    PQfinish(pool[j]);
                pool[j] = NULL;
            }
            exit(1);
        }
        pool_busy[i] = 0;
    }
}

PGconn *pool_get_connection()
{
    pthread_mutex_lock(&pool_lock);

    while (1)
    {
        for (int i = 0; i < POOL_SIZE; i++)
        {
            if (!pool_busy[i])
            {
                pool_busy[i] = 1;
                PGconn *c = pool[i];

                // auto-repair dead connections
                if (PQstatus(c) != CONNECTION_OK)
                {
                    PQreset(c);
                    if (PQstatus(c) != CONNECTION_OK)
                    {
                        fprintf(stderr, "Warning: PQreset failed: %s\n", PQerrorMessage(c));
                        // allow caller to detect error via PQresultStatus checks
                    }
                }

                pthread_mutex_unlock(&pool_lock);
                return c;
            }
        }
        pthread_cond_wait(&pool_wait, &pool_lock);
    }
}

void pool_release_connection(PGconn *c)
{
    pthread_mutex_lock(&pool_lock);

    for (int i = 0; i < POOL_SIZE; i++)
    {
        if (pool[i] == c)
        {
            pool_busy[i] = 0;
            break;
        }
    }

    pthread_cond_signal(&pool_wait);
    pthread_mutex_unlock(&pool_lock);
}

void db_update(int id, int score)
{
    PGconn *c = pool_get_connection();
    if (!c)
    {
        fprintf(stderr, "db_update: no connection available\n");
        return;
    }

    char q[256];
    snprintf(q, sizeof(q),
             "INSERT INTO leaderboard (player_id, score, last_updated) "
             "VALUES (%d, %d, now()) "
             "ON CONFLICT (player_id) DO UPDATE SET score = EXCLUDED.score, last_updated = now();",
             id, score);

    PGresult *res = PQexec(c, q);
    if (!res)
    {
        fprintf(stderr, "db_update: PQexec returned NULL\n");
    }
    else
    {
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            fprintf(stderr, "db_update: query failed: %s\n", PQerrorMessage(c));
        }
        PQclear(res);
    }

    pool_release_connection(c);
}

int db_get_top(Player *arr, int limit)
{
    PGconn *c = pool_get_connection();
    if (!c)
    {
        fprintf(stderr, "db_get_top: no connection available\n");
        return 0;
    }

    char q[128];
    snprintf(q, sizeof(q), "SELECT player_id, score FROM leaderboard ORDER BY score DESC LIMIT %d;", limit);

    PGresult *res = PQexec(c, q);
    if (!res)
    {
        fprintf(stderr, "db_get_top: PQexec returned NULL\n");
        pool_release_connection(c);
        return 0;
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        fprintf(stderr, "db_get_top: query failed: %s\n", PQerrorMessage(c));
        PQclear(res);
        pool_release_connection(c);
        return 0;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        arr[i].id = atoi(PQgetvalue(res, i, 0));
        arr[i].score = atoi(PQgetvalue(res, i, 1));
    }
    PQclear(res);
    pool_release_connection(c);
    return rows;
}

int db_get_score(int id)
{
    PGconn *c = pool_get_connection();
    if (!c)
    {
        fprintf(stderr, "db_get_score: no connection available\n");
        return -1;
    }

    char q[128];
    snprintf(q, sizeof(q),
             "SELECT score FROM leaderboard WHERE player_id=%d;", id);

    PGresult *res = PQexec(c, q);
    if (!res)
    {
        fprintf(stderr, "db_get_score: PQexec returned NULL\n");
        pool_release_connection(c);
        return -1;
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        // no rows or error
        // If it's an error, log it.
        if (PQntuples(res) == 0)
        {
            PQclear(res);
            pool_release_connection(c);
            return -1;
        }
        else
        {
            fprintf(stderr, "db_get_score: query failed: %s\n", PQerrorMessage(c));
            PQclear(res);
            pool_release_connection(c);
            return -1;
        }
    }

    int rows = PQntuples(res);
    if (rows == 0)
    {
        PQclear(res);
        pool_release_connection(c);
        return -1;
    }

    int score = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    pool_release_connection(c);
    return score;
}

// ---------- Cache Section ----------

void lru_remove(LRUNode *node)
{
    if (!node)
        return;

    if (node->prev)
        node->prev->next = node->next;
    else
        head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        tail = node->prev;
}

void lru_push_front(LRUNode *node)
{
    node->next = head;
    node->prev = NULL;
    if (head)
        head->prev = node;
    head = node;
    if (tail == NULL)
        tail = node;
}

void cache_update(int id, int score)
{
    pthread_mutex_lock(&cache_lock);

    LRUNode *node = NULL;
    HASH_FIND_INT(cache_map, &id, node);

    /* If exists, update and move to front */
    if (node)
    {
        node->score = score;
        lru_remove(node);
        lru_push_front(node);
        pthread_mutex_unlock(&cache_lock);
        return;
    }

    /* Evict if full */
    if (cache_count >= MAX_CACHE_SIZE)
    {
        /* remove tail from hash and LRU list */
        LRUNode *old_tail = tail;
        if (old_tail)
        {
            HASH_DEL(cache_map, old_tail);
            lru_remove(old_tail);
            free(old_tail);
            cache_count--;
        }
    }

    /* Create new node */
    node = (LRUNode *)malloc(sizeof(LRUNode));
    if (!node)
    {
        pthread_mutex_unlock(&cache_lock);
        return;
    }
    node->id = id;
    node->score = score;
    node->prev = node->next = NULL;

    /* insert into LRU front and into hash */
    lru_push_front(node);
    HASH_ADD_INT(cache_map, id, node);
    cache_count++;

    pthread_mutex_unlock(&cache_lock);
}

int cmp_desc(const void *a, const void *b)
{
    const Player *pa = a, *pb = b;
    return pb->score - pa->score;
}

int cache_get_top(Player *out, int limit)
{
    pthread_mutex_lock(&cache_lock);

    /* temp array sized by cache_count (bounded by MAX_CACHE_SIZE) */
    int n = 0;
    Player temp[MAX_CACHE_SIZE];
    LRUNode *cur = head;
    while (cur && n < cache_count)
    {
        temp[n].id = cur->id;
        temp[n].score = cur->score;
        cur = cur->next;
        n++;
    }

    /* sort by score desc */
    qsort(temp, n, sizeof(Player), cmp_desc);

    int ret = (n < limit) ? n : limit;
    if (ret > 0)
        memcpy(out, temp, ret * sizeof(Player));
    pthread_mutex_unlock(&cache_lock);
    return ret;
}

int cache_get_score(int id)
{
    pthread_mutex_lock(&cache_lock);

    LRUNode *node = NULL;
    HASH_FIND_INT(cache_map, &id, node);

    if (node)
    {
        int score = node->score;
        // Move to front (LRU update)
        lru_remove(node);
        lru_push_front(node);
        pthread_mutex_unlock(&cache_lock);
        return score;
    }

    pthread_mutex_unlock(&cache_lock);
    return -1;
}

// ---------- HTTP Handlers ----------
static enum MHD_Result handle_request(void *cls, struct MHD_Connection *conn_http,
                                      const char *url, const char *method,
                                      const char *ver, const char *upload_data,
                                      size_t *upload_data_size, void **con_cls)
{
    if (strcmp(method, "GET") == 0 && strncmp(url, "/leaderboard", 12) == 0)
    {
        long long start = now_us(); // measure start

        const char *top_q = MHD_lookup_connection_value(conn_http, MHD_GET_ARGUMENT_KIND, "top");
        int top = top_q ? atoi(top_q) : DEFAULT_TOP;

        Player top_players[DEFAULT_TOP];
        int count = 0;
        int cache_hit = 0;

        count = db_get_top(top_players, top);

        // if (mode == 0)
        // {
        //     count = db_get_top(top_players, top);
        //     cache_hit = 0; // DB path
        // }
        // else if (mode == 1)
        // {
        //     count = cache_get_top(top_players, top);
        //     cache_hit = 1;
        // }
        // else
        // {
        //     // hybrid: try cache first, then DB
        //     count = cache_get_top(top_players, top);
        //     if (count == 0)
        //     {
        //         count = db_get_top(top_players, top);
        //         cache_hit = 0;
        //     }
        //     else
        //     {
        //         cache_hit = 1;
        //     }
        // }

        long long end = now_us();
        printf("[LEADERBOARD] mode=%d cache_hit=%d latency=%lld us\n",
               mode, cache_hit, (end - start));
        fflush(stdout);

        // build JSON
        char json[4096];
        size_t pos = 0;
        pos += snprintf(json + pos, sizeof(json) - pos, "{\"leaderboard\":[");
        for (int i = 0; i < count && pos < sizeof(json); i++)
        {
            pos += snprintf(json + pos, sizeof(json) - pos, "{\"id\":%d,\"score\":%d}%s",
                            top_players[i].id, top_players[i].score, (i == count - 1) ? "" : ",");
        }
        pos += snprintf(json + pos, sizeof(json) - pos, "]}");

        struct MHD_Response *res = MHD_create_response_from_buffer(strlen(json), strdup(json), MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(res, "Content-Type", "application/json");
        int ret = MHD_queue_response(conn_http, MHD_HTTP_OK, res);
        MHD_destroy_response(res);
        return ret;
    }

    if (strcmp(method, "POST") == 0 && strncmp(url, "/update_score", 13) == 0)
    {
        long long start = now_us(); // start timing

        const char *id_q = MHD_lookup_connection_value(conn_http, MHD_GET_ARGUMENT_KIND, "player_id");
        const char *score_q = MHD_lookup_connection_value(conn_http, MHD_GET_ARGUMENT_KIND, "score");
        if (!id_q || !score_q)
        {
            const char *err = "Missing parameters";
            struct MHD_Response *res = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
            int ret = MHD_queue_response(conn_http, MHD_HTTP_BAD_REQUEST, res);
            MHD_destroy_response(res);
            return ret;
        }

        int id = atoi(id_q);
        int score = atoi(score_q);

        int wrote_cache = 0, wrote_db = 0;

        if (mode == 0)
        {
            db_update(id, score);
            wrote_db = 1;
        }
        else if (mode == 1)
        {
            cache_update(id, score);
            wrote_cache = 1;
        }
        else
        { // hybrid
            cache_update(id, score);
            db_update(id, score);
            wrote_cache = 1;
            wrote_db = 1;
        }

        long long end = now_us();
        printf("[UPDATE] mode=%d wrote_cache=%d wrote_db=%d latency=%lld us (id=%d score=%d)\n",
               mode, wrote_cache, wrote_db, (end - start), id, score);
        fflush(stdout);

        const char *ok = "{\"status\":\"ok\"}";
        struct MHD_Response *res = MHD_create_response_from_buffer(strlen(ok), (void *)ok, MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(conn_http, MHD_HTTP_OK, res);
        MHD_destroy_response(res);
        return ret;
    }

    if (strcmp(method, "GET") == 0 && strncmp(url, "/get_score", 10) == 0)
    {
        long long start = now_us(); // measure start

        const char *id_q = MHD_lookup_connection_value(conn_http, MHD_GET_ARGUMENT_KIND, "player_id");
        if (!id_q)
        {
            const char *err = "{\"error\":\"missing player_id\"}";
            struct MHD_Response *res = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
            int ret = MHD_queue_response(conn_http, MHD_HTTP_BAD_REQUEST, res);
            MHD_destroy_response(res);
            return ret;
        }

        int id = atoi(id_q);
        int score = -1;
        int cache_hit = 0;

        if (mode == 0)
        {
            score = db_get_score(id);
        }
        else if (mode == 1)
        {
            score = cache_get_score(id);
            cache_hit = (score >= 0) ? 1 : 0;
        }
        else
        { // hybrid
            score = cache_get_score(id);
            if (score >= 0)
            {
                cache_hit = 1;
            }
            else
            {
                score = db_get_score(id);
                cache_hit = 0;
            }
        }

        long long end = now_us();
        printf("[GET] mode=%d cache_hit=%d latency=%lld us (id=%d score=%d)\n",
               mode, cache_hit, (end - start), id, score);
        fflush(stdout);

        char json[128];
        snprintf(json, sizeof(json), "{\"id\":%d,\"score\":%d,\"cache_hit\":%d}", id, score, cache_hit);

        struct MHD_Response *res = MHD_create_response_from_buffer(strlen(json), strdup(json), MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(res, "Content-Type", "application/json");
        int ret = MHD_queue_response(conn_http, MHD_HTTP_OK, res);
        MHD_destroy_response(res);
        return ret;
    }

    const char *nf = "Not Found";
    struct MHD_Response *res = MHD_create_response_from_buffer(strlen(nf), (void *)nf, MHD_RESPMEM_PERSISTENT);
    int ret = MHD_queue_response(conn_http, MHD_HTTP_NOT_FOUND, res);
    MHD_destroy_response(res);
    return ret;
}

// ---------- MAIN ----------
static struct MHD_Daemon *http_daemon;

void cleanup(int sig)
{
    // finish pool connections
    for (int i = 0; i < POOL_SIZE; i++)
    {
        if (pool[i])
        {
            PQfinish(pool[i]);
            pool[i] = NULL;
        }
    }

    if (http_daemon)
        MHD_stop_daemon(http_daemon);
    printf("\nServer stopped.\n");
    exit(0);
}

int main(int argc, char **argv)
{
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    mode = (argc > 2) ? atoi(argv[2]) : 2;

    printf("Starting server on port %d, mode=%d\n", port, mode);

    if (mode != 1)
        pool_init(); // DB required for modes 0 and 2

    signal(SIGINT, cleanup);

    http_daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        port,
        NULL, NULL,
        &handle_request, NULL,
        MHD_OPTION_THREAD_POOL_SIZE, 20,
        MHD_OPTION_END);
    if (!http_daemon)
    {
        fprintf(stderr, "Failed to start HTTP server\n");
        return 1;
    }

    while (1)
        pause();
    return 0;
}