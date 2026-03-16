#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
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
volatile sig_atomic_t sig_count = 0;
volatile sig_atomic_t last_sig = 0;

// usage template
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s [n]\n n from [0,9]\n", name);
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
    sig_count++;
    last_sig = sig;
    //printf("[%d] Received %d sigs, pid: %d\n", getpid(), sig_count, getpid());
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

char* create_path(pid_t const pid) {
    char const *dir = "./temp/";
    char const *ext = ".txt";
    char name[20];
    sprintf(name, "%d", pid);

    char *result = malloc(sizeof(char) * (strlen(dir) + strlen(ext) + strlen(name) + 1));
    strcpy(result, dir);
    strcat(result, name);
    strcat(result, ext);

    return result;
}

// work for child process
void child_work(int const n) {
    srand(getpid());
    int s = rand() % 91 + 10;
    printf("[%d] Child, n = %d, s = %d, parent pid: [%d]\n", getpid(), n, s, getppid());

    // get KB size
    s = 1024 * s;
    char *path = create_path(getpid());

    // create file
    int const fd = TEMP_FAILURE_RETRY(open(path, O_CREAT | O_WRONLY, 0777));
    if (fd < 0)
        ERR("open");
    free(path);

    // create buffer to write
    char *buf = malloc(s);
    for (int i = 0; i < s; i++)
        buf[i] = '0' + n;

    // create mask for sigsuspend
    sigset_t mask;
    sigemptyset(&mask);

    // sleep 1 second
    struct timespec req = {1,0};
    struct timespec rem = {0,0};

    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        }
        else {
            ERR("nanosleep");
        }
    }

    last_sig = 0;
    // wait for SIGUSR1
    while (last_sig != SIGUSR1) {
        sigsuspend(&mask);
    }

    // write sig_count blocks
    for (int i = 0; i < sig_count; i++) {
        ssize_t const count = bulk_write(fd, buf, s);
        if (count < 0)
            ERR("write");
    }

    // close file and free memory
    free(buf);
    if (TEMP_FAILURE_RETRY(close(fd)))
        ERR("close");
}

// create one child and make them do child_work()
// returns pid of child to parent
pid_t create_child(int n) {
    pid_t const ret = fork();

    switch (ret) {
        case 0:
            set_handler(sig_handler, SIGUSR1);
            child_work(n);
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
    // we want to ignore SIGUSR1 in parent
    set_handler(SIG_IGN, SIGUSR1);

    // 10ms:
    struct timespec const t = {0, 10 * 1000000};

    // send signal in intervals of 10ms during 1sec (100 times)
    for (int i = 0; i < 100; i++) {
        nanosleep(&t, NULL);
        if (kill(0,SIGUSR1) < 0)
            ERR("kill");
    }
}

// main
int main(int argc, char** argv) {
    // check correct arg count
    if (argc < 2) {
        usage(argv[0]);
    }

    // set handler for SIGCHLD (correct waiting for children)
    set_handler(sigchld_handler, SIGCHLD);

    printf("[%d] Creating %d child processes\n", getpid(), argc-1);
    // get arguments (cast char* to int)
    for (int i = 1; i < argc; i++) {
        int const n = (int)strtol(argv[i], NULL, 10);
        create_child(n);
    }

    parent_work();

    // never exit without waiting for all children!
    while (wait(NULL) > 0) {}
    return EXIT_SUCCESS;
}