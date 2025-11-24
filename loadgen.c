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
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} CpuStats;

CpuStats read_cpu()
{
    CpuStats s = {0};
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return s;

    fscanf(f, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
           &s.user, &s.nice, &s.system, &s.idle,
           &s.iowait, &s.irq, &s.softirq, &s.steal);
    fclose(f);
    return s;
}

double cpu_usage_percent(CpuStats a, CpuStats b)
{
    unsigned long long idle_a = a.idle + a.iowait;
    unsigned long long idle_b = b.idle + b.iowait;

    unsigned long long nonidle_a = a.user + a.nice + a.system +
                                   a.irq + a.softirq + a.steal;
    unsigned long long nonidle_b = b.user + b.nice + b.system +
                                   b.irq + b.softirq + b.steal;

    unsigned long long total_a = idle_a + nonidle_a;
    unsigned long long total_b = idle_b + nonidle_b;

    double totald = (double)(total_b - total_a);
    double idled = (double)(idle_b - idle_a);

    return (100.0 * (totald - idled) / totald);
}

typedef struct
{
    unsigned long long read_bytes;
    unsigned long long write_bytes;
} IoStats;

IoStats read_io()
{
    IoStats io = {0};
    FILE *f = fopen("/proc/self/io", "r");
    if (!f)
        return io;

    char key[64];
    while (fscanf(f, "%63s %llu", key, &io.read_bytes) != EOF)
    {
        if (strcmp(key, "read_bytes:") == 0)
            fscanf(f, "%llu", &io.read_bytes);
        if (strcmp(key, "write_bytes:") == 0)
            fscanf(f, "%llu", &io.write_bytes);
    }

    fclose(f);
    return io;
}

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

    CpuStats c1 = read_cpu();
    IoStats io1 = read_io();

    double start = now_ms();

    /* run loadgen test */
    for (int i = 0; i < threads; i++)
        pthread_create(&tids[i], NULL, worker, &args);

    for (int i = 0; i < threads; i++)
        pthread_join(tids[i], NULL);

    double end = now_ms();

    CpuStats c2 = read_cpu();
    IoStats io2 = read_io();

    double cpu_percent = cpu_usage_percent(c1, c2);
    unsigned long long read_delta = io2.read_bytes - io1.read_bytes;
    unsigned long long write_delta = io2.write_bytes - io1.write_bytes;

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
    printf("CPU Utilization: %.2f %%\n", cpu_percent);

    curl_global_cleanup();
    return 0;
}
