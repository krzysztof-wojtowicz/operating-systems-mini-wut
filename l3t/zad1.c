#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef unsigned int UINT;
typedef struct argsEstimation
{
    pthread_t tid;
    UINT seed;
    int samplesCount;
    int i;
} argsEstimation_t;

void *thread_routine(void* arg);

void usage(char** argv) {
    fprintf(stderr, "%s takes two arguments:\n\tk - thread count,\n\tn - estimations count.\n", argv[0]);
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

    // args for estimations
    srand(time(NULL));
    argsEstimation_t *estimations = malloc(sizeof(argsEstimation_t) * k);
    if (estimations == NULL)
        ERR("malloc");

    // generate seeds before multi threading
    for (int i = 0; i < k; i++) {
        estimations[i].samplesCount = n;
        estimations[i].seed = rand();
        estimations[i].i = i;
    }

    // create threads
    for (int i = 0; i < k; i++) {
        if ((pthread_create(&(estimations[i].tid), NULL, thread_routine, &estimations[i])) != 0)
            ERR("pthread_create");
    }

    // join threads
    double cumulativeRes = 0.0;
    for (int i = 0; i < k; i++) {
        double *ret;
        if ((pthread_join(estimations[i].tid, (void *)&ret)) != 0) {
            ERR("pthread_join");
        }

        // get result
        if (ret != NULL) {
            cumulativeRes += *ret;
            free(ret);
        }
    }

    // print result
    printf("[%lu] PI ~= %f\n", pthread_self(), cumulativeRes / k);

    free(estimations);
    return EXIT_SUCCESS;
}

// estimate pi with Monte-Carlo method
void *thread_routine(void* arg) {
    argsEstimation_t args = *(argsEstimation_t *)arg;
    printf("[%lu] Worker thread %d, n = %d.\n", pthread_self(), args.i, args.samplesCount);

    double *res = malloc(sizeof(double));
    if (res == NULL)
        ERR("malloc");

    int insideCount = 0;
    for (int i = 0; i < args.samplesCount; i++)
    {
        double x = ((double)rand_r(&args.seed) / (double)RAND_MAX);
        double y = ((double)rand_r(&args.seed) / (double)RAND_MAX);
        if (sqrt(x * x + y * y) <= 1.0)
            insideCount++;
    }

    *res = 4.0 * (double)insideCount / (double)args.samplesCount;
    return res;
}