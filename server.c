/*
Compile:
gcc -O2 -Wall server.c -o server -lmicrohttpd -lpq -pthread

Usage:
./server <port> <mode>
mode = 0 (DB-only), 1 (LRU Cache + Top-N Cache only), 2 (LRU Cache + DB), 3 (All: LRU Cache + Top-N Cache + DB)
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
#define POOL_SIZE 64
#define TOP_N_SIZE 100 // Keep top 100 scores in sorted cache

typedef struct LRUNode
{
    int id;
    int score;
    struct LRUNode *prev, *next;
    UT_hash_handle hh;
} LRUNode;

typedef struct
{
    int id;
    int score;
} Player;

// Top-N Cache Structure (sorted array)
typedef struct
{
    Player players[TOP_N_SIZE];
    int count; // actual number of entries (0 to TOP_N_SIZE)
} TopNCache;

static PGconn *pool[POOL_SIZE];
static int pool_busy[POOL_SIZE];
pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pool_wait = PTHREAD_COND_INITIALIZER;

static LRUNode *head = NULL, *tail = NULL;
static int cache_count = 0;
static LRUNode *cache_map = NULL;
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

// Top-N Cache
static TopNCache topn_cache = {{0}, 0};
pthread_mutex_t topn_lock = PTHREAD_MUTEX_INITIALIZER;

static int mode = 0; // 0=DB-only, 1=Caches-only, 2=LRU+DB, 3=All

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

                if (PQstatus(c) != CONNECTION_OK)
                {
                    PQreset(c);
                    if (PQstatus(c) != CONNECTION_OK)
                    {
                        fprintf(stderr, "Warning: PQreset failed: %s\n", PQerrorMessage(c));
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

// ---------- LRU Cache Section ----------

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

    if (node)
    {
        node->score = score;
        lru_remove(node);
        lru_push_front(node);
        pthread_mutex_unlock(&cache_lock);
        return;
    }

    if (cache_count >= MAX_CACHE_SIZE)
    {
        LRUNode *old_tail = tail;
        if (old_tail)
        {
            HASH_DEL(cache_map, old_tail);
            lru_remove(old_tail);
            free(old_tail);
            cache_count--;
        }
    }

    node = (LRUNode *)malloc(sizeof(LRUNode));
    if (!node)
    {
        pthread_mutex_unlock(&cache_lock);
        return;
    }
    node->id = id;
    node->score = score;
    node->prev = node->next = NULL;

    lru_push_front(node);
    HASH_ADD_INT(cache_map, id, node);
    cache_count++;

    pthread_mutex_unlock(&cache_lock);
}

int cache_get_score(int id)
{
    pthread_mutex_lock(&cache_lock);

    LRUNode *node = NULL;
    HASH_FIND_INT(cache_map, &id, node);

    if (node)
    {
        int score = node->score;
        lru_remove(node);
        lru_push_front(node);
        pthread_mutex_unlock(&cache_lock);
        return score;
    }

    pthread_mutex_unlock(&cache_lock);
    return -1;
}

// ---------- Top-N Cache Section ----------

// Initialize Top-N cache from database
void topn_init_from_db()
{
    pthread_mutex_lock(&topn_lock);

    Player temp[TOP_N_SIZE];
    int count = db_get_top(temp, TOP_N_SIZE);

    topn_cache.count = count;
    for (int i = 0; i < count; i++)
    {
        topn_cache.players[i] = temp[i];
    }

    printf("Top-N cache initialized with %d players from DB\n", count);
    pthread_mutex_unlock(&topn_lock);
}

// Check if score qualifies for top-N (must hold topn_lock before calling)
int is_topn_score(int score)
{
    // If cache not full, any score qualifies
    if (topn_cache.count < TOP_N_SIZE)
        return 1;

    // Check if score is higher than lowest in cache
    return score > topn_cache.players[topn_cache.count - 1].score;
}

// Update Top-N cache (sorted array, must hold topn_lock)
void topn_update(int id, int score)
{
    pthread_mutex_lock(&topn_lock);

    // Find if player already exists in top-N
    int existing_idx = -1;
    for (int i = 0; i < topn_cache.count; i++)
    {
        if (topn_cache.players[i].id == id)
        {
            existing_idx = i;
            break;
        }
    }

    // If exists, remove old entry
    if (existing_idx >= 0)
    {
        for (int i = existing_idx; i < topn_cache.count - 1; i++)
        {
            topn_cache.players[i] = topn_cache.players[i + 1];
        }
        topn_cache.count--;
    }

    // Check if new score qualifies for top-N
    if (!is_topn_score(score) && existing_idx < 0)
    {
        pthread_mutex_unlock(&topn_lock);
        return;
    }

    // Find insertion position
    int pos = 0;
    for (pos = 0; pos < topn_cache.count; pos++)
    {
        if (score > topn_cache.players[pos].score)
            break;
    }

    // Shift elements down
    if (topn_cache.count < TOP_N_SIZE)
    {
        for (int i = topn_cache.count; i > pos; i--)
        {
            topn_cache.players[i] = topn_cache.players[i - 1];
        }
        topn_cache.count++;
    }
    else
    {
        // Cache full, shift and discard last
        for (int i = TOP_N_SIZE - 1; i > pos; i--)
        {
            topn_cache.players[i] = topn_cache.players[i - 1];
        }
    }

    // Insert new entry
    topn_cache.players[pos].id = id;
    topn_cache.players[pos].score = score;

    pthread_mutex_unlock(&topn_lock);
}

// Get top N from cache
int topn_get_top(Player *out, int limit)
{
    pthread_mutex_lock(&topn_lock);

    int ret = (topn_cache.count < limit) ? topn_cache.count : limit;
    if (ret > 0)
        memcpy(out, topn_cache.players, ret * sizeof(Player));

    pthread_mutex_unlock(&topn_lock);
    return ret;
}

// ---------- HTTP Handlers ----------
static enum MHD_Result handle_request(void *cls, struct MHD_Connection *conn_http,
                                      const char *url, const char *method,
                                      const char *ver, const char *upload_data,
                                      size_t *upload_data_size, void **con_cls)
{
    if (strcmp(method, "GET") == 0 && strncmp(url, "/leaderboard", 12) == 0)
    {
        long long start = now_us();

        const char *top_q = MHD_lookup_connection_value(conn_http, MHD_GET_ARGUMENT_KIND, "top");
        int top = top_q ? atoi(top_q) : DEFAULT_TOP;

        Player top_players[DEFAULT_TOP];
        int count = 0;
        int cache_hit = 0;

        if (mode == 0)
        {
            // DB-only
            count = db_get_top(top_players, top);
            cache_hit = 0;
        }
        else if (mode == 1)
        {
            // Caches-only: use Top-N cache
            count = topn_get_top(top_players, top);
            cache_hit = 1;
        }
        else if (mode == 2)
        {
            // LRU Cache + DB: use DB for leaderboard (LRU doesn't guarantee top-N)
            count = db_get_top(top_players, top);
            cache_hit = 0;
        }
        else if (mode == 3)
        {
            // All: use Top-N cache
            count = topn_get_top(top_players, top);
            cache_hit = 1;
        }

        long long end = now_us();
        printf("[LEADERBOARD] mode=%d cache_hit=%d latency=%lld us\n",
               mode, cache_hit, (end - start));
        fflush(stdout);

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
        long long start = now_us();

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

        int wrote_lru = 0, wrote_topn = 0, wrote_db = 0;

        if (mode == 0)
        {
            // DB-only
            db_update(id, score);
            wrote_db = 1;
        }
        else if (mode == 1)
        {
            // Caches-only: update both LRU and Top-N caches
            cache_update(id, score);
            topn_update(id, score);
            wrote_lru = 1;
            wrote_topn = 1;
        }
        else if (mode == 2)
        {
            // LRU Cache + DB: update LRU and DB
            cache_update(id, score);
            db_update(id, score);
            wrote_lru = 1;
            wrote_db = 1;
        }
        else if (mode == 3)
        {
            // All: update LRU, Top-N, and DB
            cache_update(id, score);
            topn_update(id, score);
            db_update(id, score);
            wrote_lru = 1;
            wrote_topn = 1;
            wrote_db = 1;
        }

        long long end = now_us();
        printf("[UPDATE] mode=%d lru=%d topn=%d db=%d latency=%lld us (id=%d score=%d)\n",
               mode, wrote_lru, wrote_topn, wrote_db, (end - start), id, score);
        fflush(stdout);

        const char *ok = "{\"status\":\"ok\"}";
        struct MHD_Response *res = MHD_create_response_from_buffer(strlen(ok), (void *)ok, MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(conn_http, MHD_HTTP_OK, res);
        MHD_destroy_response(res);
        return ret;
    }

    if (strcmp(method, "GET") == 0 && strncmp(url, "/get_score", 10) == 0)
    {
        long long start = now_us();

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
            // DB-only
            score = db_get_score(id);
            cache_hit = 0;
        }
        else if (mode == 1)
        {
            // Caches-only: try LRU cache
            score = cache_get_score(id);
            cache_hit = (score >= 0) ? 1 : 0;
        }
        else if (mode == 2)
        {
            // LRU Cache + DB: try LRU first, fallback to DB
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
        else if (mode == 3)
        {
            // All: try LRU first, fallback to DB
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
    mode = (argc > 2) ? atoi(argv[2]) : 3;

    printf("Starting server on port %d, mode=%d\n", port, mode);

    // Initialize DB pool for modes 0, 2, 3
    if (mode == 0 || mode == 2 || mode == 3)
    {
        pool_init();
        printf("DB pool initialized\n");
    }

    // Initialize Top-N cache for modes 1 and 3
    if (mode == 1 || mode == 3)
    {
        if (mode == 3)
        {
            // Mode 3: Initialize from DB
            topn_init_from_db();
        }
        else
        {
            // Mode 1: Empty cache (will be populated as updates come)
            printf("Top-N cache initialized (empty, cache-only mode)\n");
        }
    }

    printf("\n=== Mode Configuration ===\n");
    if (mode == 0)
        printf("Mode 0: DB-only\n");
    else if (mode == 1)
        printf("Mode 1: LRU Cache + Top-N Cache (no DB)\n");
    else if (mode == 2)
        printf("Mode 2: LRU Cache + DB (no Top-N cache)\n");
    else if (mode == 3)
        printf("Mode 3: LRU Cache + Top-N Cache + DB (All)\n");
    printf("=========================\n\n");

    signal(SIGINT, cleanup);

    http_daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        port,
        NULL, NULL,
        &handle_request, NULL,
        MHD_OPTION_THREAD_POOL_SIZE, 10,
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