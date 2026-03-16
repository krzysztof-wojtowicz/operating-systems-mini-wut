#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define PLAYER_COUNT 4
#define ROUNDS 5

typedef struct args {
    unsigned int seed;
    int i;
    pthread_t tid;
    pthread_barrier_t *barrier;
    int *scores;
    int *throws;
} args_t;

void *player(void *arg);

int main (int argc, char **argv) {
    fprintf(stdout, "!!! DICE GAME !!!\n\n");

    /* INIT */
    // arguments for players
    args_t args[PLAYER_COUNT];

    // scores array
    int scores[PLAYER_COUNT];

    // throws
    int throws[PLAYER_COUNT];

    // random engine init
    srand(time(NULL));

    // barrier init
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, PLAYER_COUNT);
    /* INIT */

    /* CREATE PLAYERS */
    // create threads
    for (int i = 0; i < PLAYER_COUNT; i++) {
        scores[i] = 0;

        args[i].i = i;
        args[i].seed = rand();
        args[i].scores = scores;
        args[i].throws = throws;
        args[i].barrier = &barrier;

        if (pthread_create(&args[i].tid, NULL, player, &args[i]) != 0)
            ERR("pthread_create");
    }
    /* CREATE PLAYERS */

    /* JOIN THREADS */
    for (int i = 0; i < PLAYER_COUNT; i++) {
        if (pthread_join(args[i].tid, NULL) != 0)
            ERR("pthread_join");
    }
    /* JOIN THREADS */

    /* PRINT RESULTS */
    fprintf(stdout, "\nRESULTS\n");
    for (int i = 0; i < PLAYER_COUNT; i++) {
        fprintf(stdout, "\t[P%d] %d\n", i, scores[i]);
    }
    /* PRINT RESULTS */

    /* CLEANUP & EXIT */
    pthread_barrier_destroy(&barrier);

    exit(EXIT_SUCCESS);
    /* CLEANUP & EXIT */
}

// player worker
void *player(void *arg) {
    args_t *args = (args_t *)arg;

    // play ROUNDS number of rounds
    for (int i = 0; i < ROUNDS; i++) {
        // dice throw
        args->throws[args->i] = rand_r(&args->seed) % 6 + 1;
        fprintf(stdout, "[P%d] ROUND %d, THROW = %d\n", args->i, i, args->throws[args->i]);

        // wait for every other player to do their throw
        int res = pthread_barrier_wait(args->barrier);

        // one player calculates scores
        if (res == PTHREAD_BARRIER_SERIAL_THREAD) {
            fprintf(stdout, "\n[P%d] SCORES FOR ROUND %d\n", args->i, i);
            // calculate scores
            int max = 6;
            int flag = 0;

            while (flag == 0 && max > 0) {
                for (int j = 0; j < PLAYER_COUNT; j++) {
                    if (args->throws[j] == max) {
                        args->scores[j]++;
                        fprintf(stdout, "\tP%d GOT A POINT!\n", j);
                        flag = 1;
                    }
                }
                max--;
            }

            fprintf(stdout, "\n");
        }

        // wait for scores calculations
        pthread_barrier_wait(args->barrier);
    }

    // end thread
    return NULL;
}