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
#include <signal.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef unsigned int UINT;
typedef struct timespec timespec_t;
typedef struct args_t {
    int track_size;
    sigset_t *mask;
    int *track;
    pthread_mutex_t *track_mtx;
    int *place;
    pthread_mutex_t *place_mtx;
} args_t;
typedef struct dog_args_t {
    args_t *args;
    int number;
    UINT seed;
} dog_args_t;

void usage(char** argv) {
    fprintf(stderr, "%s takes two arguments:\n\tn>20 - track length\n\tm>2 - dogs count\n", argv[0]);
    exit(EXIT_FAILURE);
}

void msleep(UINT milisec);
void *dog_run(void *arg);
void *signal_handler(void *arg);
void print_track(int *track, int n);

int main (int argc, char** argv) {
    // check argc
    if (argc < 3)
        usage(argv);

    // get n & m from argv
    int n = (int)strtol(argv[1], NULL, 10);
    if (n <= 20)
        usage(argv);
    int m = (int)strtol(argv[2], NULL, 10);
    if (m <= 2)
        usage(argv);

    printf("DOG RACE!!! track length = %d, dogs = %d\n", n, m);

    // prepare track & mtxs
    int track[n], place = 1;
    pthread_mutex_t track_mtx[n], place_mtx;
    for (int i = 0; i < n; i++) {
        track[i] = 0;
        pthread_mutex_init(&track_mtx[i], NULL);
    }
    pthread_mutex_init(&place_mtx, NULL);

    // prepare mask for signals
    sigset_t mask;
    sigfillset(&mask);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // prepare dog_args
    srand(time(NULL));
    args_t *args = malloc(sizeof(args_t));
    if (args == NULL)
        ERR("malloc");

    args->track_size = n;
    args->mask = &mask;
    args->place = &place;
    args->place_mtx = &place_mtx;
    args->track = track;
    args->track_mtx = track_mtx;

    dog_args_t dog_args[m];
    for (int i = 0; i < m; i++) {
        dog_args[i].number = i;
        dog_args[i].seed = rand();
        dog_args[i].args = args;
    }

    // start dogs
    pthread_t tid[m];
    for (int i = 0; i < m; i++) {
        if (pthread_create(&tid[i], NULL, dog_run, &dog_args[i]) != 0)
            ERR("pthread_create");
    }

    // create signal handler
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t stid;
    int stop_flag = 0;

    if (pthread_create(&stid, &attr, signal_handler, &stop_flag) != 0)
        ERR("pthread_create");

    // print track state
    while (1) {
        if (stop_flag == 1) {
            // SIGINT, suspend race, cancel all dogs
            for (int i = 0; i < m; i++) {
                pthread_cancel(tid[i]);
            }
            break;
        }

        msleep(1000);
        print_track(track, n);

        pthread_mutex_lock(&place_mtx);
        if (place > m) {
            pthread_mutex_unlock(&place_mtx);
            break;
        }
        pthread_mutex_unlock(&place_mtx);
    }

    // join threads
    int best_dogs[3];
    for (int i = 0; i < m; i++) {
        int *dog_place;
        if (pthread_join(tid[i], (void*)&dog_place) != 0)
            ERR("pthread_join");

        // save best 3 dogs numbers
        if (dog_place != PTHREAD_CANCELED && *dog_place < 4) {
            best_dogs[*dog_place-1] = i;
        }

        if (dog_place != PTHREAD_CANCELED)
            free(dog_place);
    }

    if (stop_flag == 0) {
        // prints podium
        printf("RACE ENDED!\nResults:\n\t1st place: Dog %d\n\t", best_dogs[0]);
        printf("2nd place: Dog %d\n\t3rd place: Dog %d\n", best_dogs[1], best_dogs[2]);
    }
    else if (stop_flag == 1) {
        printf("RACE SUSPENDED!\n");
    }

    // cleanup and exit
    pthread_mutex_destroy(&place_mtx);
    for (int i = 0; i < m; i++) {
        pthread_mutex_destroy(&track_mtx[i]);
    }
    pthread_attr_destroy(&attr);

    if (stop_flag == 0)
        exit(EXIT_SUCCESS);
    else
        exit(EXIT_FAILURE);
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

void *dog_run(void *arg) {
    dog_args_t *dog_args = (dog_args_t *)arg;
    int track_size = dog_args->args->track_size;

    // block all signals
    if (pthread_sigmask(SIG_BLOCK, dog_args->args->mask, NULL) != 0)
        ERR("pthread_sigmask");

    // dog init
    int pos = 0;
    pthread_mutex_lock(&dog_args->args->track_mtx[0]);
    dog_args->args->track[0] += 1;
    pthread_mutex_unlock(&dog_args->args->track_mtx[0]);

    // race!
    while (1) {
        // sleep random time [200,1520] ms
        int ms = rand_r(&dog_args->seed) % 1321 + 200;
        msleep(ms);

        // get next position from [1,5]
        int next_pos = pos + (rand_r(&dog_args->seed) % 5 + 1);
        // if outside track boundaries -> change direction
        if (next_pos >= track_size) {
            next_pos = track_size - 1 - (next_pos % (track_size - 1));
        }

        // check if there is a dog on next position, unless it is a finish
        pthread_mutex_lock(&dog_args->args->track_mtx[next_pos]);
        if (next_pos != track_size-1 && dog_args->args->track[next_pos] != 0) {
            printf("[Dog %d] waf waf waf\n", dog_args->number);
        }
        // if not, change position and update track state
        else {
            pthread_mutex_lock(&dog_args->args->track_mtx[pos]);
            dog_args->args->track[next_pos] += 1;
            dog_args->args->track[pos] -= 1;
            pthread_mutex_unlock(&dog_args->args->track_mtx[pos]);
            pos = next_pos;
            printf("[Dog %d] new position %d\n", dog_args->number, next_pos);

        }
        pthread_mutex_unlock(&dog_args->args->track_mtx[next_pos]);

        // if finish -> end work
        if (pos == track_size-1) {
            break;
        }
    }

    // get dog place at the finish
    int *place = malloc(sizeof(int));
    if (place == NULL)
        ERR("malloc");

    pthread_mutex_lock(dog_args->args->place_mtx);
    *place = *(dog_args->args->place);
    *(dog_args->args->place) += 1;
    pthread_mutex_unlock(dog_args->args->place_mtx);

    printf("[Dog %d] finish line! Place: %d\n", dog_args->number, *place);

    return place;
}

void *signal_handler(void *arg) {
    int *stop_flag = (int *)arg;

    // create set mask for sigwait
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    int signo;

    // sigwait loop
    for (;;) {
        if (sigwait(&mask, &signo) != 0)
            ERR("sigwait");

        switch (signo) {
            case SIGINT:
                *(stop_flag) = 1;
                return NULL;
            default:
                break;
        }
    }
}

void print_track(int *track, int n) {
    printf("Track: ");
    for (int i = 0; i < n; i++) {
        printf("%d ", track[i]);
        //printf("%d[%d] ", i, track[i]);
    }
    printf("\n");
}