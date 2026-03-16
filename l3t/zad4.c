#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ELAPSED(start, end) ((end).tv_sec - (start).tv_sec) + (((end).tv_nsec - (start).tv_nsec) * 1.0e-9)
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char** argv) {
    fprintf(stderr, "%s takes one argument:\n\tn<=100 - students count\n", argv[0]);
    exit(EXIT_FAILURE);
}

typedef unsigned int UINT;
typedef struct timespec timespec_t;
typedef struct args_t {
    int *cnt, *exp;
    pthread_mutex_t *cnt_mtx, *exp_mtx;
} args_t;

void *thread_work(void* arg);
void thread_cleanup(void* arg);
void msleep(UINT milisec);

int main (int argc, char** argv) {
    // TO JEST ZLE W CHUJ GENERALNIE

    // check argc
    if (argc < 2)
        usage(argv);

    // get n from argv
    int n = (int)strtol(argv[1], NULL, 10);
    if (n <= 0 || n > 100)
        usage(argv);
    printf("[%lu] Hello from main, n = %d\n", pthread_self(), n);

    // counts for each year, engineers and expelled students
    int cnt[4];
    int exp = 0;

    // mtx for each year count
    pthread_mutex_t cnt_mtx[4], exp_mtx;
    for (int i = 0; i < 4; i++) {
        cnt[i] = 0;
        pthread_mutex_init(&cnt_mtx[i], NULL);
    }
    pthread_mutex_init(&exp_mtx, NULL);

    // tids array
    pthread_t tid[n];

    // create args for threads
    args_t *args = malloc(sizeof(args_t));
    if (args == NULL)
        ERR("malloc");

    args->cnt = cnt;
    args->cnt_mtx = cnt_mtx;
    args->exp = &exp;
    args->exp_mtx = &exp_mtx;

    // create threads
    for (int i = 0; i < n; i++) {
        if (pthread_create(&tid[i], NULL, thread_work, args) != 0)
            ERR("pthread_create");
    }

    // expelling loop
    int j = 0;
    timespec_t start, current;
    if (clock_gettime(CLOCK_REALTIME, &start))
        ERR("clock_gettime");
    do
    {
        // sleep
        msleep(rand() % 201 + 100);
        if (clock_gettime(CLOCK_REALTIME, &current))
            ERR("clock_gettime");

        // kick student
        if (j < n) {
            pthread_cancel(tid[j]);
            j++;
        }
    } while (ELAPSED(start, current) < 4.0);

    // join threads
    for (int i = 0; i < n; i++) {
        if (pthread_join(tid[i], NULL) != 0)
            ERR("pthread_join");
    }

    // prints results
    printf("Engineers: %d\nExpelled: %d\n", cnt[3], exp);

    // cleanup and exit
    for (int i = 0; i < 4; i++)
        pthread_mutex_destroy(&cnt_mtx[i]);
    pthread_mutex_destroy(&exp_mtx);
    exit(EXIT_SUCCESS);
}

void *thread_work(void* arg) {
    args_t *args = (args_t *)arg;
    pthread_cleanup_push(thread_cleanup, args);

    printf("[%lu] New student\n", pthread_self());

    for (int i = 0; i < 4; i++) {
        // add to next year
        pthread_mutex_lock(&(args->cnt_mtx[i]));
        args->cnt[i] += 1;
        pthread_mutex_unlock(&(args->cnt_mtx[i]));
        // subtract from previous year
        if (i > 0) {
            pthread_mutex_lock(&(args->cnt_mtx[i-1]));
            args->cnt[i-1] -= 1;
            pthread_mutex_unlock(&(args->cnt_mtx[i-1]));
        }
        // sleep
        msleep(1000);
    }

    pthread_cleanup_pop(0);
    return NULL;
}

void thread_cleanup(void *arg) {
    args_t *args = (args_t *)arg;

    // expelled student
    pthread_mutex_lock(args->exp_mtx);
    *(args->exp) += 1;
    pthread_mutex_unlock(args->exp_mtx);
}

void msleep(UINT milisec)
{
    time_t sec = (int)(milisec / 1000);
    milisec = milisec - (sec * 1000);
    timespec_t req = {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if (nanosleep(&req, &req))
        ERR("nanosleep");
}