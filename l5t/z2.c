#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_BUFF 200

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, " 0<n<=10 - number of children\n");
    exit(EXIT_FAILURE);
}

int set_handler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigchld_handler(int sig)
{
    pid_t pid;
    for (;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (0 == pid)
            return;
        if (0 >= pid)
        {
            if (ECHILD == errno)
                return;
            ERR("waitpid");
        }
    }
}

void sigint_handler(int sig) {last_signal = sig;}

void sigkill_handler(int sig) {
    // 20% chance to kill child process
    if (rand() % 5 == 0) {
        printf("Child exiting...\n");
        exit(EXIT_SUCCESS);
    }

}

void child_work(int r_pipe, int w_pipe, int i) {
    // set SIGINT handler for child
    set_handler(sigkill_handler, SIGINT);

    // print info
    printf("[%d] Child %d started\n", getpid(), i);

    // random gen init
    srand(getpid());

    ssize_t count;
    char c;
    char buf[MAX_BUFF];

    // read one char from pipe & send it to parent
    while ((count = TEMP_FAILURE_RETRY(read(r_pipe, &c, 1))) == 1) {
        // get random number from [1,200]
        int v = rand() % MAX_BUFF + 1;

        // write char to buf
        for (int j = 0; j < v; j++)
            buf[j] = c;

        // set remaining part of buf to 0
        memset(buf + v, 0, MAX_BUFF - v);

        // write to pipe
        if (TEMP_FAILURE_RETRY(write(w_pipe, buf, MAX_BUFF)) < 0)
            ERR("write pipe");
    }

    if (count < 0)
        ERR("read pipe");

    return;
}

void parent_work(int r_pipe, int n, int w_pipes[n]) {
    // set SIGINT handler
    set_handler(sigint_handler, SIGINT);

    // random gen init
    srand(getpid());

    // print info
    printf("[%d] Parent started\n", getpid());

    ssize_t count;
    char buf[MAX_BUFF];
    char c;

    // main loop
    while (1) {
        if (last_signal == SIGINT) {
            // ger random pipe number & random char
            int pipe_i = rand() % n;

            // find an open pipe
            while (w_pipes[pipe_i % n] == 0 && pipe_i < 2*n) {}
            pipe_i %= n;

            if (w_pipes[pipe_i]) {
                c = rand() % ('z' - 'a') + 'a';

                // write this char to pipe
                if (TEMP_FAILURE_RETRY(write(w_pipes[pipe_i], &c, 1)) != 1) {
                    if (TEMP_FAILURE_RETRY(close(w_pipes[pipe_i])) < 0)
                        ERR("close");

                    w_pipes[pipe_i] = 0;
                }

                printf("Parent sent %c to child %d\n", c, pipe_i);
            }

            last_signal = 0;
        }

        // read buf from r_pipe
        if ((count = read(r_pipe, buf, MAX_BUFF)) > 0)
            printf("%s\n", buf);

        // check if there was an interruption
        if (errno == EINTR)
            continue;

        // pipe closed
        if (count <= 0)
            break;
    }

    return;
}

int main (int argc, char** argv) {
    // check arguments
    if (argc < 2)
        usage(argv[0]);

    // get number of processes
    int n = strtol(argv[1], NULL, 10);
    if (n <= 0 || n > 10)
        usage(argv[0]);

    // set SIGCHLD handler & ignore SIGINT, SIGPIPE
    if (set_handler(sigchld_handler, SIGCHLD) < 0)
        ERR("set handler");
    if (set_handler(SIG_IGN, SIGINT) < 0)
        ERR("set handler");
    if (set_handler(SIG_IGN, SIGPIPE) < 0)
        ERR("set handler");

    // create pipes for children
    int pipes_fds[n];
    int pipe_R[2];
    if (pipe(pipe_R) < 0)
        ERR("pipe");

    // create n child processes, each with its own pipe
    pid_t pids[n];
    int temp_fd[2];
    for (int i = 0; i < n; i++) {
        // create pipe for this child
        if (pipe(temp_fd) < 0)
            ERR("child pipe");

        pipes_fds[i] = temp_fd[1];

        // fork
        pids[i] = fork();

        switch (pids[i]) {
            case 0:
                // close unused descriptors of previous children
                for (int j = i; j >= 0; j--)
                    if (close(pipes_fds[j]))
                        ERR("close pipe");

                // close unused descriptor of parent (reading)
                if (close(pipe_R[0]))
                    ERR("close pipe");

                // start child with pipes for reading and writing
                child_work(temp_fd[0], pipe_R[1], i);

                // close remaining descriptors
                if (close(temp_fd[0]))
                    ERR("close pipe");

                if (close(pipe_R[1]))
                    ERR("close pipe");

                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                // close unused descriptor (child reading)
                if (close(temp_fd[0]))
                    ERR("close pipe");
                break;
        }
    }

    // close unused descriptor for writing
    if (close(pipe_R[1]))
        ERR("close pipe");

    parent_work(pipe_R[0], n, pipes_fds);

    // close remaining descriptors
    for (int i = 0; i < n; i++)
        if (close(pipes_fds[i]))
            ERR("close pipe");

    if (close(pipe_R[0]))
        ERR("close pipe");

    printf("Exiting...\n");

    return EXIT_SUCCESS;
}