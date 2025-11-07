/*
Compile:
gcc -O2 -Wall server_simple.c -o server -lmicrohttpd -lpq

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

#define MAX_PLAYERS 10000
#define DEFAULT_PORT 8080
#define DEFAULT_TOP 10

typedef struct
{
    int id;
    int score;
} Player;

static Player cache[MAX_PLAYERS];
static int cache_size = 0;
static PGconn *conn = NULL;
static int mode = 0; // 0=DB, 1=Cache, 2=Hybrid

long long now_us()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

// ---------- DB Section ----------
void init_db()
{
    conn = PQconnectdb("host=127.0.0.1 port=5432 dbname=leaderboard_db user=leaderboard_user password=leaderboard_pw");
    if (PQstatus(conn) != CONNECTION_OK)
    {
        fprintf(stderr, "DB Connection failed: %s\n", PQerrorMessage(conn));
        exit(1);
    }
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS leaderboard (player_id INT PRIMARY KEY, score INT, last_updated TIMESTAMP DEFAULT now());";
    PQexec(conn, create_sql);
    printf("Connected to DB: %s\n", PQdb(conn));
    printf("User: %s\n", PQuser(conn));
    printf("Host: %s\n", PQhost(conn));
    printf("Port: %s\n", PQport(conn));
}

void db_update(int id, int score)
{
    char q[256];
    snprintf(q, sizeof(q),
             "INSERT INTO leaderboard (player_id, score, last_updated) "
             "VALUES (%d, %d, now()) "
             "ON CONFLICT (player_id) DO UPDATE SET score = EXCLUDED.score, last_updated = now();",
             id, score);

    PGresult *res = PQexec(conn, q);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "DB ERROR on update: %s\nQuery: %s\n", PQerrorMessage(conn), q);
    }
    else
    {
        printf("[DB WRITE] player_id=%d score=%d\n", id, score);
    }

    PQclear(res);
}

int db_get_top(Player *arr, int limit)
{
    char q[128];
    snprintf(q, sizeof(q), "SELECT player_id, score FROM leaderboard ORDER BY score DESC LIMIT %d;", limit);
    PGresult *res = PQexec(conn, q);
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        arr[i].id = atoi(PQgetvalue(res, i, 0));
        arr[i].score = atoi(PQgetvalue(res, i, 1));
    }
    PQclear(res);
    return rows;
}

// ---------- Cache Section ----------
void cache_update(int id, int score)
{
    // if exists, update
    for (int i = 0; i < cache_size; i++)
    {
        if (cache[i].id == id)
        {
            cache[i].score = score;
            return;
        }
    }
    // new entry
    if (cache_size < MAX_PLAYERS)
    {
        cache[cache_size].id = id;
        cache[cache_size].score = score;
        cache_size++;
    }
}

int cmp_desc(const void *a, const void *b)
{
    const Player *pa = a, *pb = b;
    return pb->score - pa->score;
}

int cache_get_top(Player *out, int limit)
{
    qsort(cache, cache_size, sizeof(Player), cmp_desc);
    int n = (cache_size < limit) ? cache_size : limit;
    memcpy(out, cache, n * sizeof(Player));
    return n;
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

        if (mode == 0)
        {
            count = db_get_top(top_players, top);
            cache_hit = 0; // DB path
        }
        else
        {
            count = cache_get_top(top_players, top);
            cache_hit = 1; // Cache path
        }

        long long end = now_us();
        printf("[LEADERBOARD] mode=%d cache_hit=%d latency=%lld us\n",
               mode, cache_hit, (end - start));
        fflush(stdout);

        // build JSON
        char json[2048] = "{\"leaderboard\":[";
        for (int i = 0; i < count; i++)
        {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "{\"id\":%d,\"score\":%d}%s",
                     top_players[i].id, top_players[i].score, (i == count - 1) ? "" : ",");
            strcat(json, tmp);
        }
        strcat(json, "]}");

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
    if (conn)
        PQfinish(conn);
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
        init_db(); // DB required for modes 0 and 2

    signal(SIGINT, cleanup);

    http_daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
                                   &handle_request, NULL, MHD_OPTION_END);
    if (!http_daemon)
    {
        fprintf(stderr, "Failed to start HTTP server\n");
        return 1;
    }

    while (1)
        pause();
    return 0;
}
