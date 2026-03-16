#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n>0 t>0\n", name);
    exit(EXIT_FAILURE);
}

void set_handler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sig_handler(int const sig) {
    last_signal = sig;
}

void sigchld_handler(int const sig) {
    pid_t pid;

    while ((pid = waitpid(0, NULL, WNOHANG)) != -1) {
        // no children waiting
        if (pid == 0)
            return;
    }

    if (errno != ECHILD)
        ERR("waitpid");
}

void child_work(int t, int n) {
    printf("[%d] Child process with parent pid: %d\n", getpid(), getppid());
    int signal_count = 0;
    struct timespec tt = {0, t * 10000};

    while (signal_count < 25)
    {
        for (int i = 0; i < n; i++)
        {
            nanosleep(&tt, NULL);
            if (kill(getppid(), SIGUSR1))
                ERR("kill");
        }
        nanosleep(&tt, NULL);
        if (kill(getppid(), SIGUSR2))
            ERR("kill");
        signal_count++;
        printf("[%d] Signals SIGUSR2 sent: %d\n", getpid(), signal_count);
    }
    printf("[%d] Terminates\n", getpid());
}

void create_child(int t, int n) {
    pid_t const ret = fork();

    switch (ret) {
        case 0:
            child_work(t, n);
            exit(EXIT_SUCCESS);
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        default:
            break;
    }
}

void parent_work(sigset_t oldmask) {
    int signal_count = 0;

    while (signal_count < 25) {
        last_signal = 0;
        while (last_signal != SIGUSR2)
            sigsuspend(&oldmask);

        signal_count++;
        printf("[%d] Received %d signals SIGUSR2\n", getpid(), signal_count);
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        usage(argv[0]);
    }

    // arguments
    int t = strtol(argv[1], NULL, 10);
    int n = strtol(argv[2], NULL, 10);
    if (n <= 0 || t <= 0) {
        usage(argv[0]);
    }

    // set handlers for SIGCHLD, SIGUSR1, SIGUSR2
    set_handler(sigchld_handler, SIGCHLD);
    set_handler(sig_handler, SIGUSR1);
    set_handler(sig_handler, SIGUSR2);

    // block SIGUSR1, SIGUSR2
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    printf("[%d] Creating %d child process\n", getpid(), 1);
    create_child(t, n);

    parent_work(oldmask);

    while (wait(NULL) > 0) {}

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    return EXIT_SUCCESS;
}