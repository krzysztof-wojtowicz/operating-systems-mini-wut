#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BIN_COUNT 11
#define NEXT_DOUBLE(seedptr) ((double)rand_r(seedptr) / (double)RAND_MAX)
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef unsigned int UINT;
typedef struct args_t {
    pthread_mutex_t left_mtx;
    pthread_mutex_t thrown_mtx;
    pthread_mutex_t *bins_mtx;
    int *bins;
    int *balls_left;
    int *balls_thrown;
    UINT seed;
} args_t;

void *thread_routine(void* arg);
int throw_ball(UINT *seedptr);

void usage(char** argv) {
    fprintf(stderr, "%s takes two arguments:\n\tk - thread count,\n\tn - balls count.\n", argv[0]);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    // check argc
    if (argc < 3) {
        usage(argv);
    }

    // get args
    int k = (int)strtol(argv[1], NULL, 10);
    int n = (int)strtol(argv[2], NULL, 10);
    if (k <= 0 || n <= 0) {
        usage(argv);
    }
    printf("[%lu] Hello from main, k = %d, n = %d\n", pthread_self(), k, n);

    // create bins
    int *bins = malloc(sizeof(int) * BIN_COUNT);
    if (bins == NULL)
        ERR("malloc");

    pthread_mutex_t *bins_mtx = malloc(sizeof(pthread_mutex_t) * BIN_COUNT);
    if (bins_mtx == NULL)
        ERR("malloc");

    for (int i = 0; i < BIN_COUNT; i++) {
        bins[i] = 0;
        pthread_mutex_init(&bins_mtx[i], NULL);
    }

    // create attrib for threads
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // create args
    args_t *args = malloc(sizeof(args_t) * k);
    if (args == NULL)
        ERR("malloc");

    // create mutex for balls thrown and left
    pthread_mutex_t thrown_mtx, left_mtx;
    pthread_mutex_init(&thrown_mtx, NULL);
    pthread_mutex_init(&left_mtx, NULL);

    // vars for balls
    int *balls_thrown = malloc(sizeof(int));
    if (balls_thrown == NULL)
        ERR("malloc");
    *balls_thrown = 0;

    int *balls_left = malloc(sizeof(int));
    if (balls_left == NULL)
        ERR("malloc");
    *balls_left = n;

    // initialize args
    srand(time(NULL));
    for (int i = 0; i < k; i++) {
        args[i].bins = bins;
        args[i].seed = rand();
        args[i].bins_mtx = bins_mtx;
        args[i].balls_left = balls_left;
        args[i].balls_thrown = balls_thrown;
        args[i].left_mtx = left_mtx;
        args[i].thrown_mtx = thrown_mtx;
    }

    // create threads
    for (int i = 0; i < k; i++) {
        pthread_t tid;
        if ((pthread_create(&tid, &attr, thread_routine, &args[i])) != 0)
            ERR("pthread_create");
    }

    // wait for program to end
    while (1) {
        pthread_mutex_lock(&thrown_mtx);
        if (*balls_thrown == n)
            break;
        pthread_mutex_unlock(&thrown_mtx);

        sleep(1);
    }

    // print results
    printf("All balls: %d\n", *balls_thrown);
    for (int i = 0; i < BIN_COUNT; i++) {
        printf("\tBin[%d] = %d\n", i, bins[i]);
        //pthread_mutex_destroy(&bins_mtx[i]);
    }

    // free memory, with detached threads we are not sure if they finished
    // pthread_mutex_destroy(&thrown_mtx);
    // pthread_mutex_destroy(&left_mtx);
    // pthread_attr_destroy(&attr);
    // free(bins);
    // free(args);
    // free(bins_mtx);
    // free(balls_left);
    // free(balls_thrown);

    // exit to finish all threads
    exit(EXIT_SUCCESS);
}

void *thread_routine(void* arg) {
    args_t args = *(args_t *)arg;
    printf("[%lu] Worker thread.\n", pthread_self());

    while (1) {
        // check if there are any balls left to throw
        pthread_mutex_lock(&args.left_mtx);
        if (*args.balls_left > 0) {
            (*args.balls_left)--;
            pthread_mutex_unlock(&args.left_mtx);
        }
        else {
            pthread_mutex_unlock(&args.left_mtx);
            break;
        }

        // get bin number
        //int bin = rand_r(&args.seed) % 11 + 1;
        int bin = throw_ball(&args.seed);

        pthread_mutex_lock(&args.bins_mtx[bin]);
        args.bins[bin]++;
        printf("[%lu] Ball thrown to bin %d.\n", pthread_self(), bin);
        pthread_mutex_unlock(&args.bins_mtx[bin]);

        pthread_mutex_lock(&args.thrown_mtx);
        (*args.balls_thrown)++;
        pthread_mutex_unlock(&args.thrown_mtx);
    }

    return NULL;
}

int throw_ball(UINT *seedptr)
{
    int result = 0;
    for (int i = 0; i < BIN_COUNT - 1; i++)
        if (NEXT_DOUBLE(seedptr) > 0.5)
            result++;
    return result;
}