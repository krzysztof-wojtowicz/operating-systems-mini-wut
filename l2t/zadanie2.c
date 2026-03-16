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
volatile sig_atomic_t cough_count = 0;
volatile sig_atomic_t is_ill = 0;
volatile sig_atomic_t p;

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

// SIGTERM handler
void sigterm_handler(int sig) {
    printf("Child[%d] Exits with %d\n", getpid(), cough_count);
    exit(cough_count);
}

// SIGCHLD handler - correct waiting for children
void sigchld_handler(int sig)
{
    pid_t pid;
    int stat;
    while (1)
    {
        pid = waitpid(0, &stat, WNOHANG);

        if (pid == 0) {
            return;
        }

        if (pid > 0) {
            printf("KG:[%d] Child %d coughed %d times\n", getpid(), pid, stat);
        }

        if (pid <= 0)
        {
            if (errno == ECHILD)
                return;
            ERR("waitpid");
        }
    }
}

// SIGUSR1 handler - coughing
void sigusr1_handler(int sig) {
    if (!is_ill) {
        int s = rand() % 100;
        if (s < p)
            is_ill = 1;
        printf("Child[%d]: %d has coughed at me!\n", getpid(), 0);
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
    printf("Child[%d]: starts day in the kindergarten, ill: %d\n", getpid(), is_ill);
    srand(getpid());

    while (1) {
        if (is_ill) {
            unsigned int ms = rand() % 201 + 800;
            ms_sleep(ms);
            cough_count++;
            printf("Child[%d] is coughing (%d)\n", getpid(), cough_count);
            kill(0,SIGUSR1);
        }
    }
}

// create one child and make them do child_work()
// returns pid of child to parent
pid_t create_child(int ill) {
    pid_t const ret = fork();

    switch (ret) {
        case 0:
            set_handler(sigterm_handler, SIGTERM);
            set_handler(sigusr1_handler, SIGUSR1);
            is_ill = ill;
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
void parent_work(sigset_t oldmask) {
    sigsuspend(&oldmask);

    kill(0,SIGTERM);
}

// main
int main(int argc, char** argv) {
    // check correct arg count
    if (argc != 5) {
        usage(argv[0]);
    }

    // get arguments (cast char* to int)
    int t = (int)strtol(argv[1], NULL, 10); // 1-100
    //int k = (int)strtol(argv[2], NULL, 10); // 1-100
    int n = (int)strtol(argv[3], NULL, 10); // 1-30
    p = (int)strtol(argv[4], NULL, 10); // 1-100
    //int pids[n];
    //int stats[n];

    // set handler for SIGCHLD (correct waiting for children)
    set_handler(sigchld_handler, SIGCHLD);
    set_handler(sig_handler, SIGALRM);
    set_handler(SIG_IGN, SIGTERM);
    set_handler(SIG_IGN, SIGUSR1);

    create_child(1);
    for (int i = 1; i < n; i++)
        create_child(0);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);

    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    // set alarm
    alarm(t);
    printf("KG[%d]: Alarm has been set for %d sec\n", getpid(), t);
    parent_work(oldmask);

    // never exit without waiting for all children!
    int stat, pid;
    while ((pid = wait(&stat)) > 0) {
        printf("KG:[%d] Child %d coughed %d times\n", getpid(), pid, stat);
    }

    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    return EXIT_SUCCESS;
}