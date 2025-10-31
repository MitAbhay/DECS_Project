/*
Compile:
gcc -O2 -pthread loadgen.c -o loadgen -lcurl

But we'll use simple sockets via libcurl would be easier. To reduce dependencies we use libcurl.
Install: sudo apt install libcurl4-openssl-dev
Compile: gcc -O2 -pthread loadgen.c -o loadgen -lcurl
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/time.h>

typedef struct
{
    char server_url[256];
    int thread_id;
    int updates_perc; // percentage of requests that are updates
    int top_n;
    int runtime; // seconds per thread
    long requests_sent;
    long requests_ok;
    double total_latency_ms;
} thread_arg_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) { return size * nmemb; }

static double now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void *worker(void *arg)
{
    thread_arg_t *t = (thread_arg_t *)arg;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    srand(time(NULL) + t->thread_id);
    double end_time = now_ms() + t->runtime * 1000.0;
    while (now_ms() < end_time)
    {
        int r = rand() % 100;
        double start = now_ms();
        if (r < t->updates_perc)
        {
            // POST update
            int pid = rand() % 100000;
            int score = rand() % 1000000;
            char url[512];
            snprintf(url, sizeof(url), "%s/update_score?player_id=%d&score=%d", t->server_url, pid, score);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
            CURLcode res = curl_easy_perform(curl);
            t->requests_sent++;
            if (res == CURLE_OK)
                t->requests_ok++;
        }
        else
        {
            // GET leaderboard
            char url[512];
            snprintf(url, sizeof(url), "%s/leaderboard?top=%d", t->server_url, t->top_n);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            CURLcode res = curl_easy_perform(curl);
            t->requests_sent++;
            if (res == CURLE_OK)
                t->requests_ok++;
        }
        double lat = now_ms() - start;
        t->total_latency_ms += lat;
        // zero think time for saturation tests; you can add nanosleep if needed
    }
    curl_easy_cleanup(curl);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        fprintf(stderr, "Usage: %s <server_url> <num_threads> <upt_pct> <top_n> <run_seconds>\n", argv[0]);
        fprintf(stderr, "upt_pct = percentage updates (0-100)\n");
        return 1;
    }
    char server[256];
    strcpy(server, argv[1]);
    int num_threads = atoi(argv[2]);
    int upd_pct = atoi(argv[3]);
    int top_n = atoi(argv[4]);
    int run_seconds = atoi(argv[5]);

    curl_global_init(CURL_GLOBAL_ALL);
    pthread_t threads[num_threads];
    thread_arg_t args[num_threads];
    for (int i = 0; i < num_threads; i++)
    {
        snprintf(args[i].server_url, sizeof(args[i].server_url), "%s", server);
        args[i].thread_id = i;
        args[i].updates_perc = upd_pct;
        args[i].top_n = top_n;
        args[i].runtime = run_seconds;
        args[i].requests_sent = 0;
        args[i].requests_ok = 0;
        args[i].total_latency_ms = 0.0;
        pthread_create(&threads[i], NULL, worker, &args[i]);
    }
    long total_sent = 0, total_ok = 0;
    double total_latency = 0.0;
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
        total_sent += args[i].requests_sent;
        total_ok += args[i].requests_ok;
        total_latency += args[i].total_latency_ms;
    }
    double avg_latency = (total_ok > 0) ? (total_latency / total_ok) : 0.0;
    printf("Threads: %d, Total sent: %ld, OK: %ld, Throughput(req/s): %.2f, Avg latency(ms): %.2f\n",
           num_threads, total_sent, total_ok, (double)total_ok / run_seconds, avg_latency);
    curl_global_cleanup();
    return 0;
}
