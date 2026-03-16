#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n > 0\n", name);
    exit(EXIT_FAILURE);
}

void createChild() {
    pid_t ret = fork();

    switch (ret) {
        case 0:
            srand(time(NULL) * getpid());
            sleep(rand() % 6 + 5); // sleep for [5,10] seconds
            printf("[%d] Child process with parent pid: %d\n", getpid(), getppid());
            exit(EXIT_SUCCESS);
        case -1:
            ERR("fork");
        default:
            break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
    }

    int n = strtol(argv[1], NULL, 10);
    if (n <= 0) {
        usage(argv[0]);
    }

    printf("[%d] Creating %d child processes\n", getpid(), n);

    for (int i = 0; i < n; i++) {
        createChild();
    }

    int pid;

    while ((pid = waitpid(-1, NULL, WNOHANG)) != -1) {
        // child returned something
        if (pid != 0) {
            n--;
        }
        printf("[%d] Waiting for %d children...\n", getpid(), n);
        sleep(3);
    }

    if (errno != ECHILD) {
        ERR("waitpid");
    }

    return EXIT_SUCCESS;
}