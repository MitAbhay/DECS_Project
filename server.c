/*
Compile with:
gcc -O2 -pthread server.c skiplist.c -lmicrohttpd -lpq -o server
*/

#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <libpq-fe.h>
#include "skiplist.h"
#include "config.h"

#define POSTBUFFERSIZE 512
#define MAX_CLIENTS 1000

// Modes
typedef enum
{
    MODE_DB_ONLY = 0,
    MODE_CACHE_ONLY = 1,
    MODE_HYBRID = 2
} run_mode_t;

static run_mode_t g_mode = MODE_HYBRID;
static skiplist *g_sl = NULL;
static pthread_mutex_t sl_lock = PTHREAD_MUTEX_INITIALIZER;

// PostgreSQL connection
static PGconn *g_conn = NULL;

// cache hit/miss counters
static long cache_hits = 0;
static long cache_misses = 0;
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

static int server_port = DEFAULT_PORT;

void init_pg()
{
    g_conn = PQconnectdb(PG_CONNINFO);
    if (PQstatus(g_conn) != CONNECTION_OK)
    {
        fprintf(stderr, "Postgres connection failed: %s\n", PQerrorMessage(g_conn));
        exit(1);
    }
    // ensure table exists
    const char *create_sql = "CREATE TABLE IF NOT EXISTS leaderboard (player_id INTEGER PRIMARY KEY, score INTEGER, last_updated TIMESTAMP DEFAULT now());";
    PGresult *res = PQexec(g_conn, create_sql);
    PQclear(res);
}

void close_pg()
{
    if (g_conn)
        PQfinish(g_conn);
}

int db_update_score(int player_id, int score)
{
    char query[256];
    // upsert
    snprintf(query, sizeof(query),
             "INSERT INTO leaderboard (player_id, score, last_updated) VALUES (%d, %d, now()) ON CONFLICT (player_id) DO UPDATE SET score = EXCLUDED.score, last_updated = now();",
             player_id, score);
    PGresult *res = PQexec(g_conn, query);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "DB error: %s\n", PQerrorMessage(g_conn));
        PQclear(res);
        return -1;
    }
    PQclear(res);
    return 0;
}

int db_get_top_n(int n, int *players, int *scores)
{
    char query[256];
    snprintf(query, sizeof(query), "SELECT player_id, score FROM leaderboard ORDER BY score DESC LIMIT %d;", n);
    PGresult *res = PQexec(g_conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return -1;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        players[i] = atoi(PQgetvalue(res, i, 0));
        scores[i] = atoi(PQgetvalue(res, i, 1));
    }
    PQclear(res);
    return rows;
}

static int iterate_and_write_json(struct MHD_Connection *connection, int *players, int *scores, int cnt)
{
    // build simple JSON
    char *json;
    size_t approx = cnt * 32 + 64;
    json = malloc(approx);
    strcpy(json, "{\"leaderboard\":[");
    for (int i = 0; i < cnt; i++)
    {
        char entry[64];
        snprintf(entry, sizeof(entry), "{\"player_id\":%d,\"score\":%d}%s", players[i], scores[i], (i == cnt - 1) ? "" : ",");
        strcat(json, entry);
    }
    strcat(json, "]}");
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(json), (void *)json, MHD_RESPMEM_MUST_FREE);
    int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

static int answer_to_connection(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls)
{

    if (strcmp(method, "GET") == 0 && strncmp(url, "/leaderboard", 12) == 0)
    {
        // parse query ?top=N
        const char *top_q = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "top");
        int top = top_q ? atoi(top_q) : DEFAULT_TOP_N;
        int players[1000];
        int scores[1000];
        int cnt = 0;
        if (g_mode == MODE_DB_ONLY)
        {
            // fetch from DB
            cnt = db_get_top_n(top, players, scores);
            // no cache stat changes (cache not used)
        }
        else if (g_mode == MODE_CACHE_ONLY)
        {
            pthread_mutex_lock(&sl_lock);
            cnt = sl_get_top_n(g_sl, top, players, scores);
            pthread_mutex_unlock(&sl_lock);
            pthread_mutex_lock(&stats_lock);
            cache_hits++;
            pthread_mutex_unlock(&stats_lock);
        }
        else
        { // hybrid
            pthread_mutex_lock(&sl_lock);
            cnt = sl_get_top_n(g_sl, top, players, scores);
            pthread_mutex_unlock(&sl_lock);
            if (cnt > 0)
            {
                pthread_mutex_lock(&stats_lock);
                cache_hits++;
                pthread_mutex_unlock(&stats_lock);
            }
            else
            {
                pthread_mutex_lock(&stats_lock);
                cache_misses++;
                pthread_mutex_unlock(&stats_lock);
                // read from db and populate cache
                cnt = db_get_top_n(top, players, scores);
                pthread_mutex_lock(&sl_lock);
                for (int i = 0; i < cnt; i++)
                    sl_insert(g_sl, players[i], scores[i]);
                pthread_mutex_unlock(&sl_lock);
            }
        }
        return iterate_and_write_json(connection, players, scores, cnt);
    }
    else if (strcmp(method, "POST") == 0 && strncmp(url, "/update_score", 13) == 0)
    {
        // parse body or query params. For ease, we accept query params
        const char *pid_q = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "player_id");
        const char *score_q = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "score");
        if (!pid_q || !score_q)
        {
            const char *bad = "Missing player_id or score";
            struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(bad), (void *)bad, MHD_RESPMEM_PERSISTENT);
            int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
            MHD_destroy_response(resp);
            return ret;
        }
        int pid = atoi(pid_q);
        int score = atoi(score_q);
        // mode behavior:
        if (g_mode == MODE_DB_ONLY)
        {
            db_update_score(pid, score);
        }
        else if (g_mode == MODE_CACHE_ONLY)
        {
            pthread_mutex_lock(&sl_lock);
            sl_insert(g_sl, pid, score);
            pthread_mutex_unlock(&sl_lock);
        }
        else
        { // hybrid
            // update memory and async persist
            pthread_mutex_lock(&sl_lock);
            sl_insert(g_sl, pid, score);
            pthread_mutex_unlock(&sl_lock);
            // synchronous DB write for simplicity (you may change to async thread queue)
            db_update_score(pid, score);
        }
        const char *ok = "{\"status\":\"ok\"}";
        struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(ok), (void *)ok, MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
        MHD_destroy_response(resp);
        return ret;
    }
    else
    {
        const char *not = "Not found";
        struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(not), (void *)not, MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
        MHD_destroy_response(resp);
        return ret;
    }
}

static struct MHD_Daemon *d;

void sigint_handler(int sig)
{
    if (d)
        MHD_stop_daemon(d);
    close_pg();
    if (g_sl)
        sl_free(g_sl);
    printf("\nServer shutting down.\nCache hits: %ld, misses: %ld\n", cache_hits, cache_misses);
    exit(0);
}

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    if (argc >= 2)
        port = atoi(argv[1]);
    server_port = port;
    if (argc >= 3)
    {
        int mode = atoi(argv[2]);
        if (mode >= 0 && mode <= 2)
            g_mode = (run_mode_t)mode;
    }
    printf("Starting server on port %d, mode=%d\n", port, g_mode);
    signal(SIGINT, sigint_handler);

    // init
    if (g_mode != MODE_CACHE_ONLY)
        init_pg();
    if (g_mode != MODE_DB_ONLY)
        g_sl = sl_create();

    d = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port, NULL, NULL, &answer_to_connection, NULL, MHD_OPTION_THREAD_POOL_SIZE, THREAD_POOL_SIZE, MHD_OPTION_END);
    if (!d)
    {
        fprintf(stderr, "Failed to start HTTP daemon\n");
        return 1;
    }
    // run forever
    while (1)
        sleep(1);
    return 0;
}
