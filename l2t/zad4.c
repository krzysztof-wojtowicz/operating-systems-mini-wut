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

volatile sig_atomic_t sig_count = 0;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s t n s name\nt - time intervals, n - block count, s - block size, name - file name\n", name);
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
    sig_count++;
}

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

void child_work(int t) {
    printf("[%d] Child process with parent pid: %d\n", getpid(), getppid());
    int signal_count = 0;
    struct timespec tt = {0,t*10000};

    while (1)
    {
        nanosleep(&tt, NULL);
        if (kill(getppid(), SIGUSR1))
            ERR("kill");

        signal_count++;
        printf("[%d] Signal SIGUSR1 sent: %d\n", getpid(), signal_count);
    }
}

pid_t create_child(int t) {
    pid_t const ret = fork();

    switch (ret) {
        case 0:
            child_work(t);
            exit(EXIT_SUCCESS);
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        default:
            break;
    }

    return ret;
}

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

void parent_work(int n, int s, char* name) {
    char *buf = malloc(s);
    if (buf == NULL)
        ERR("malloc");

    int out = TEMP_FAILURE_RETRY(open(name, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0777));
    if (out < 0)
        ERR("open");

    int in = TEMP_FAILURE_RETRY(open("/dev/urandom", O_RDONLY));
    if (in < 0)
        ERR("open");

    ssize_t count;
    for (int i = 0; i < n; i++) {
        // count = TEMP_FAILURE_RETRY(read(in, buf, s));
        // count = TEMP_FAILURE_RETRY(write(out, buf, s));

        if ((count = bulk_read(in, buf, s)) < 0)
            ERR("read");

        if ((count = bulk_write(out, buf, count)) < 0)
            ERR("read");

        if (TEMP_FAILURE_RETRY(fprintf(stderr, "[%d] Block of %ld bytes transferred. Signals RX:%d\n", getpid(), count, sig_count)) < 0)
            ERR("fprintf");
    }

    if (TEMP_FAILURE_RETRY(close(in)))
        ERR("close");

    if (TEMP_FAILURE_RETRY(close(out)))
        ERR("close");

    free(buf);
}

int main(int argc, char** argv) {
    if (argc != 5) {
        usage(argv[0]);
    }

    // arguments
    int t = strtol(argv[1], NULL, 10);
    int n = strtol(argv[2], NULL, 10);
    int s = strtol(argv[3], NULL, 10);
    char* name = argv[4];

    if (n <= 0 || t <= 0 || s <= 0) {
        usage(argv[0]);
    }

    // set handlers for SIGCHLD, SIGUSR1
    set_handler(sigchld_handler, SIGCHLD);
    set_handler(sig_handler, SIGUSR1);

    // create child which sends signals SIGUSR1 to parent
    printf("[%d] Creating %d child process\n", getpid(), 1);
    pid_t pid = create_child(t);

    // create file
    parent_work(n, s*1024*1024, name);

    // kill child
    kill(pid, SIGUSR2);

    while (wait(NULL) > 0) {}

    // print number of SIGUSR1 received
    printf("[%d] Received %d SIGUSR1 signals\n", getpid(), sig_count);

    return EXIT_SUCCESS;
}