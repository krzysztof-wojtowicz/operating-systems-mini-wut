#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define FS_NUM 5
#define MAX_INPUT 120

typedef unsigned int UINT;
typedef struct timespec timespec_t;
typedef struct args {
    UINT milisec;
    sem_t *sem;
    int i;
} args_t;
typedef struct sargs {
    int *last_signal;
} sargs_t;

void msleep(UINT milisec);
void *signal_handler(void *arg);
void *alarm_worker(void *arg);

int main (int argc, char** argv) {
    fprintf(stdout, "Program creates alarm threads for specifed number of seconds [>0 <=120]\n");
    fprintf(stdout, "Maximum alarms threads running at the same time: %d\n", FS_NUM);

    // buffer for reading user input
    char *buf = NULL;
    size_t len = 0;

    // args for signal handler
    int last_signal = 0;
    sargs_t sargs;
    sargs.last_signal = &last_signal;

    // block signals in main thread
    sigset_t mask;
    sigfillset(&mask);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // signal handler thread, create detached
    pthread_t signal_tid;
    if (pthread_create(&signal_tid, NULL, signal_handler, &sargs) != 0)
        ERR("pthread_create");
    if (pthread_detach(signal_tid) != 0)
        ERR("pthread_detach");

    // semaphor for workers
    sem_t sem;
    if (sem_init(&sem, 0, FS_NUM) != 0)
        ERR("sem_init");

    // tid and index for alarms
    pthread_t tid;
    int i = 0;

    // work loop
    while (last_signal != SIGINT && last_signal != SIGQUIT) {
        fprintf(stdout, "> ");
        if (getline(&buf, &len, stdin) > 1) {
            if (len > 1) {
                int sec = strtol(buf, NULL, 10);
                if (sec <= 0 || sec > MAX_INPUT)
                    fprintf(stdout, "Invalid input [%d]!\n", sec);

                // create new worker thread
                args_t *args = malloc(sizeof(args_t));
                if (args == NULL)
                    ERR("malloc");
                args->milisec =  sec*1000;
                args->sem = &sem;
                args->i = i;

                // try to create new thread, check semaphor
                if (sem_trywait(&sem) != -1) {
                    // create alarm thread
                    if (pthread_create(&tid, NULL, alarm_worker, args) != 0)
                        ERR("pthread_create");
                    // detach alarm thread
                    if (pthread_detach(tid) != 0)
                        ERR("pthread_detach");
                    i++;
                } else {
                    fprintf(stdout, "Only %d alarms can be set at the time.\n", FS_NUM);
                }
            }
        }
        free(buf);
        buf = NULL;
        len = 0;
    }

    // exit if SIGINT or SIGQUIT
    exit(EXIT_SUCCESS);
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

void *signal_handler(void *arg) {
    int *last_signal = ((sargs_t*)arg)->last_signal;

    // mask for sigwait
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);

    // sigwait loop
    int sig_no;

    for (;;) {
        if (sigwait(&mask, &sig_no))
            ERR("sigwait");

        *last_signal = sig_no;

        // if sigint or sigquit end thread
        if (sig_no == SIGINT || sig_no == SIGQUIT)
            return NULL;
    }
}

void *alarm_worker(void *arg) {
    args_t *args = (args_t *)arg;

    // block signals in worker thread
    sigset_t mask;
    sigfillset(&mask);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // alarm logic
    fprintf(stdout, "\n[Alarm %d] New alarm for %d seconds.\n> ", args->i, args->milisec/1000);
    fflush(stdout);
    msleep(args->milisec);
    fprintf(stdout, "\n[Alarm %d] Wake up!\n> ", args->i);
    fflush(stdout);

    // post semaphor
    if (sem_post(args->sem) == -1)
        ERR("sem_post");

    // free memory
    free(args);

    // end thread
    return NULL;
}