#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

// global variable for signals handling
volatile sig_atomic_t last_sig = 0;

// usage template
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s <usage>\n", name);
    exit(EXIT_FAILURE);
}

// setting handler with sigaction
void set_handler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

// default handler, sets global variable for last_sig
void sig_handler(int const sig) {
    last_sig = sig;
}

// SIGCHLD handler - correct waiting for children
void sigchld_handler(int sig)
{
    pid_t pid;

    while (1)
    {
        pid = waitpid(0, NULL, WNOHANG);

        if (pid == 0)
            return;

        if (pid <= 0)
        {
            if (errno == ECHILD)
                return;
            ERR("waitpid");
        }
    }
}

// bulk read from file with correct signal handling
ssize_t bulk_read(int fd, char *buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;

    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;

        if (c == 0)
            return len;  // EOF

        buf += c;
        len += c;
        count -= c;
    } while (count > 0);

    return len;
}

// bulk write to file with correct signal handling
ssize_t bulk_write(int fd, char *buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;

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

// sleep for ms
void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (TEMP_FAILURE_RETRY(nanosleep(&ts, &ts)))
        ERR("nanosleep");
}

// work for child process
void child_work() {
    printf("[%d] Child process with parent pid: %d\n", getpid(), getppid());
}

// create one child and make them do child_work()
// returns pid of child to parent
pid_t create_child() {
    pid_t const ret = fork();

    switch (ret) {
        case 0:
            child_work();
            exit(EXIT_SUCCESS);
        case -1:
            ERR("fork");
        default:
            break;
    }

    return ret;
}

// work for parent process
void parent_work() {
    printf("[%d] Parent working...\n", getpid());
}

// main
int main(int argc, char** argv) {
    // check correct arg count
    if (argc < 0) {
        usage(argv[0]);
    }

    // get arguments (cast char* to int)
    // int n = (int)strtol(argv[1], NULL, 10);
    // int n = atoi(argv[1]);

    // set handler for SIGCHLD (correct waiting for children)
    set_handler(sigchld_handler, SIGCHLD);

    printf("[%d] Creating %d child process\n", getpid(), 1);
    create_child();

    parent_work();

    // never exit without waiting for all children!
    while (wait(NULL) > 0) {}
    return EXIT_SUCCESS;
}