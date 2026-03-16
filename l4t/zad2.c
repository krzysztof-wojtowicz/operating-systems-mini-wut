#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define BUFFERSIZE 256
#define READCHUNKS 4
#define THREAD_NUM 3

typedef struct args {
    int i; // thread number
    pthread_t tid; // tid
    pthread_mutex_t *mtx; // mutex
    pthread_cond_t *cv; // conditional variable
    int *condition;
    int *last_signal;
    pthread_mutex_t *signal_mtx;
} args_t;

typedef struct sargs {
    int *last_signal;
    pthread_mutex_t *signal_mtx;
} sargs_t;

void *signal_handler(void *arg);
void *worker(void *arg);
ssize_t bulk_read(int fd, char *buf, size_t count);
ssize_t bulk_write(int fd, char *buf, size_t count);

int main(int argc, char** argv) {
    fprintf(stdout, "Program reads random bytes from /dev/random after ENTER\n");
    fprintf(stdout, "Uses %d threads, each reading to its own file.\n", THREAD_NUM);

    /* SIGNAL HANDLING */
    // args for signal handler
    int last_signal = 0;
    pthread_mutex_t signal_mtx;
    pthread_mutex_init(&signal_mtx, NULL);
    sargs_t sargs;
    sargs.last_signal = &last_signal;
    sargs.signal_mtx = &signal_mtx;

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
    /* SIGNAL HANDLING */

    /* WORKERS */
    // MTX init
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, NULL);

    // CV init
    pthread_cond_t cv;
    pthread_cond_init(&cv, NULL);

    // args init
    args_t args[THREAD_NUM];

    // create workers
    for (int i = 0; i < THREAD_NUM; i++) {
        args[i].i = i;
        args[i].cv = &cv;
        args[i].mtx = &mtx;
        args[i].last_signal = &last_signal;
        args[i].signal_mtx = &signal_mtx;

        if (pthread_create(&args[i].tid, NULL, worker, &args[i]) != 0)
            ERR("pthread_create");
    }
    /* WORKERS */

    /* USER INPUT */
    // buffer and len init
    char *buf = NULL;
    size_t len = 0;

    // reading loop
    while (1) {
        // check last signal
        pthread_mutex_lock(&signal_mtx);
        if (last_signal == SIGINT) {
            pthread_mutex_unlock(&signal_mtx);
            break;
        }
        pthread_mutex_unlock(&signal_mtx);

        // get user input
        if (getline(&buf, &len, stdin) > 0) {
            if (buf[0] == '\n') {
                fprintf(stdout, "ENTER\n");
                pthread_mutex_lock(&mtx);
                pthread_cond_signal(&cv);
                pthread_mutex_unlock(&mtx);
            }
        }

        // reset
        free(buf);
        buf = NULL;
        len = 0;
    }
    /* USER INPUT */

    /* JOIN THREADS */
    pthread_mutex_lock(&mtx);
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mtx);

    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(args[i].tid, NULL) != 0)
            ERR("pthread_join");
    }
    /* JOIN THREADS */

    /* CLEANUP & EXIT */
    pthread_mutex_destroy(&signal_mtx);
    pthread_mutex_destroy(&mtx);
    pthread_cond_destroy(&cv);

    fprintf(stderr, "EXITING");
    exit(EXIT_SUCCESS);
    /* CLEANUP & EXIT */
}

// signal handling thread
void *signal_handler(void *arg) {
    int *last_signal = ((sargs_t*)arg)->last_signal;

    // mask for sigwait
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);

    // sigwait loop
    int sig_no;

    for (;;) {
        if (sigwait(&mask, &sig_no))
            ERR("sigwait");

        *last_signal = sig_no;

        // if sigint end thread
        if (sig_no == SIGINT)
            return NULL;
    }
}

// worker
void *worker(void *arg) {
    args_t *args = (args_t *)arg;

    // block signals
    sigset_t mask;
    sigfillset(&mask);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // open thread file
    char name[20];
    sprintf(name, "./temp/thread%d", args->i);
    int fd = open(name, O_CREAT | O_WRONLY | O_TRUNC | O_APPEND, 0777);
    if (fd == -1)
        ERR("open");

    // open /dev/random
    int fdr = open("/dev/random", O_RDONLY);
    if (fdr == -1)
        ERR("open");

    // reading loop
    while (1) {
        // wait for cv (signal from main thread)
        pthread_mutex_lock(args->mtx);
        pthread_cond_wait(args->cv, args->mtx);
        pthread_mutex_unlock(args->mtx);

        // check last signal
        pthread_mutex_lock(args->signal_mtx);
        if (*(args->last_signal) == SIGINT) {
            pthread_mutex_unlock(args->signal_mtx);
            break;
        }
        pthread_mutex_unlock(args->signal_mtx);

        // read from random to thread file
        fprintf(stderr, "[THREAD %d] READ START\n", args->i);
        char buf[BUFFERSIZE];
        int count;
        if ((count = bulk_read(fdr, buf, BUFFERSIZE)) < 0)
            ERR("bulk_read");
        if ((count = bulk_write(fd, buf, count)) < 0)
            ERR("bulk_write");
        fprintf(stderr, "[THREAD %d] READ END\n", args->i);
    }

    // close files
    if (close(fd) == -1)
        ERR("close");
    if (close(fdr) == -1)
        ERR("close");

    // end thread
    return NULL;
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}