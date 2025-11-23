/*
Enhanced Load Generator
Modes:
0 = Update only
1 = Leaderboard GET only
2 = Mixed (default)

Compile:
gcc -O2 -Wall loadgen.c -o loadgen -lcurl -lpthread

Usage:
./loadgen http://127.0.0.1:8080 4 100 2
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/time.h>
#include <time.h>

typedef struct
{
    const char *base_url;
    int requests;
    int mode;
    /*0 = update only
1 = leaderboard only
2 = mixed update+leaderboard
3 = get_score only*/
} ThreadArgs;

double now_ms()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000.0 + t.tv_usec / 1000.0;
}

void *worker(void *arg)
{
    ThreadArgs *ta = (ThreadArgs *)arg;
    CURL *curl = curl_easy_init();

    if (!curl)
        pthread_exit(NULL);

    char url[256];
    for (int i = 0; i < ta->requests; i++)
    {
        int pid = rand() % 100000 + 1;
        int score = rand() % 50000;

        if (ta->mode == 0)
        {
            // UPDATE only
            snprintf(url, sizeof(url), "%s/update_score?player_id=%d&score=%d",
                     ta->base_url, pid, score);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_perform(curl);
        }
        else if (ta->mode == 1)
        {
            // GET only
            snprintf(url, sizeof(url), "%s/leaderboard?top=10",
                     ta->base_url);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_perform(curl);
        }
        else if (ta->mode == 3)
        {
            int pid = rand() % 10000 + 1;

            snprintf(url, sizeof(url), "%s/get_score?player_id=%d",
                     ta->base_url, pid);

            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_perform(curl);
        }
        else
        {
            // MIXED (update then get)
            snprintf(url, sizeof(url), "%s/update_score?player_id=%d&score=%d",
                     ta->base_url, pid, score);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_perform(curl);

            snprintf(url, sizeof(url), "%s/leaderboard?top=10", ta->base_url);
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_perform(curl);
        }
    }

    curl_easy_cleanup(curl);
    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        printf("Usage: %s <server_url> <threads> <requests_per_thread> <mode>\n", argv[0]);
        printf("mode: 0=update only, 1=get only, 2=mixed\n");
        return 1;
    }

    const char *url = argv[1];
    int threads = atoi(argv[2]);
    int reqs = atoi(argv[3]);
    int mode = atoi(argv[4]);

    srand(time(NULL));
    curl_global_init(CURL_GLOBAL_ALL);

    pthread_t tids[threads];
    ThreadArgs args = {url, reqs, mode};

    double start = now_ms();

    for (int i = 0; i < threads; i++)
        pthread_create(&tids[i], NULL, worker, &args);

    for (int i = 0; i < threads; i++)
        pthread_join(tids[i], NULL);

    double end = now_ms();

    double total;
    if (mode == 2)
        total = threads * reqs * 2; // mixed
    else
        total = threads * reqs; // others

    printf("\n=== Load Test Summary ===\n");
    printf("Mode: %d\n", mode);
    printf("Threads: %d, Requests/thread: %d\n", threads, reqs);
    printf("Total HTTP requests: %.0f\n", total);
    printf("Elapsed: %.2f sec\n", (end - start) / 1000.0);
    printf("Throughput: %.2f req/sec\n", total / ((end - start) / 1000.0));

    curl_global_cleanup();
    return 0;
}
