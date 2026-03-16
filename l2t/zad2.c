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
    fprintf(stderr, "USAGE: %s n>0 k>0 p>0 r>0\n", name);
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
    printf("[%d] Received signal %d\n", getpid(), sig);
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

void child_work(int r) {
    printf("[%d] Child process with parent pid: %d\n", getpid(), getppid());

    // get random sleep time
    srand(getpid());
    int const sleeptime = rand() % 6 + 5; // sleep for [5,10] seconds

    for (int i = 0; i < r; i++) {
        // sleep
        for (int tt = sleeptime; tt > 0; tt = sleep(tt)) {}

        if (last_signal == SIGUSR1)
            printf("[%d] SUCCESS\n", getpid());
        else if (last_signal == SIGUSR2)
            printf("[%d] FAILURE\n", getpid());
    }
    printf("[%d] Terminates\n", getpid());
}

void create_child(int const r) {
    pid_t const ret = fork();

    switch (ret) {
        case 0:
            // set handler for children
            set_handler(sig_handler, SIGUSR1);
            set_handler(sig_handler, SIGUSR2);
            child_work(r);
            exit(EXIT_SUCCESS);
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        default:
            break;
    }
}

void parent_work(int k, int p, int r) {
    struct timespec tk = {k,0};
    struct timespec tp = {p,0};

    // all children should finish after 10 * r seconds
    // so we set an alarm to finish parent process
    set_handler(sig_handler, SIGALRM);
    alarm(r * 10);

    while (last_signal != SIGALRM) {
        nanosleep(&tk, NULL);
        if (kill(0, SIGUSR1) == -1)
            ERR("kill");

        nanosleep(&tp, NULL);
        if (kill(0, SIGUSR2) == -1)
            ERR("kill");
    }
}

int main(int argc, char** argv) {
    if (argc < 5) {
        usage(argv[0]);
    }

    // arguments
    int n = strtol(argv[1], NULL, 10);
    int k = strtol(argv[2], NULL, 10);
    int p = strtol(argv[3], NULL, 10);
    int r = strtol(argv[4], NULL, 10);
    if (n <= 0 || k <= 0 || p <= 0 || r <= 0) {
        usage(argv[0]);
    }

    // block sigusr1 and sigusr2 on parent process
    // and handle SIGCHLD
    set_handler(sigchld_handler, SIGCHLD);
    set_handler(SIG_IGN, SIGUSR1);
    set_handler(SIG_IGN, SIGUSR2);

    printf("[%d] Creating %d child processes\n", getpid(), n);
    // create n child processes
    for (int i = 0; i < n; i++) {
        create_child(r);
    }

    parent_work(k,p,r);

    while (wait(NULL) > 0) {}

    return EXIT_SUCCESS;
}