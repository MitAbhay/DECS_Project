/*
Compile:
gcc -O2 -Wall loadgen_simple.c -o loadgen -lcurl -lpthread

Usage:
./loadgen <server_url> <threads> <requests_per_thread>

Example:
./loadgen http://127.0.0.1:8080 4 100
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/time.h>

typedef struct
{
    const char *base_url;
    int requests;
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
        int pid = rand() % 1000 + 1;
        int score = rand() % 50000;

        // POST update
        snprintf(url, sizeof(url), "%s/update_score?player_id=%d&score=%d", ta->base_url, pid, score);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 500L);
        curl_easy_perform(curl);

        // GET leaderboard
        snprintf(url, sizeof(url), "%s/leaderboard?top=5", ta->base_url);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_perform(curl);
    }

    curl_easy_cleanup(curl);
    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage: %s <server_url> <threads> <requests_per_thread>\n", argv[0]);
        return 1;
    }

    const char *url = argv[1];
    int threads = atoi(argv[2]);
    int reqs = atoi(argv[3]);

    srand(time(NULL));
    curl_global_init(CURL_GLOBAL_ALL);

    pthread_t tids[threads];
    ThreadArgs args = {url, reqs};

    double start = now_ms();

    for (int i = 0; i < threads; i++)
        pthread_create(&tids[i], NULL, worker, &args);

    for (int i = 0; i < threads; i++)
        pthread_join(tids[i], NULL);

    double end = now_ms();
    double total = threads * reqs * 2; // each loop = 1 update + 1 get

    printf("\n=== Load Test Summary ===\n");
    printf("Threads: %d, Requests/thread: %d\n", threads, reqs);
    printf("Total requests: %.0f\n", total);
    printf("Elapsed time: %.2f sec\n", (end - start) / 1000.0);
    printf("Throughput: %.2f req/sec\n", total / ((end - start) / 1000.0));

    curl_global_cleanup();
    return 0;
}
