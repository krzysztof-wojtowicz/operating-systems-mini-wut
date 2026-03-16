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
    (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define ITER_COUNT 25

volatile sig_atomic_t last_sig = 0;

ssize_t bulk_read(int fd, char* buf, size_t count)
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

ssize_t bulk_write(int fd, char* buf, size_t count)
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

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

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

void usage(int argc, char* argv[])
{
    printf("%s n\n", argv[0]);
    printf("\t1 <= n <= 4 -- number of moneyboxes\n");
    exit(EXIT_FAILURE);
}

void child_handler(int sig) { last_sig = sig; }

void box_handler(int sig) { last_sig = sig; }

char* get_filename(int pid)
{
    // create filename skarbona_<PID>
    char* s = "skarbona_";
    char c_pid[20];
    sprintf(c_pid, "%d", pid);

    char* filename = malloc(sizeof(char) * (strlen(s) + strlen(c_pid) + 1));
    if (filename == NULL)
        ERR("malloc");

    strcpy(filename, s);
    strcat(filename, c_pid);

    return filename;
}

void moneybox_work()
{
    printf("[%d] Skarbona otwarta\n", getpid());
    // get filename
    char* filename = get_filename(getpid());
    // open file
    int fd = TEMP_FAILURE_RETRY(open(filename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0777));
    if (fd < 0)
        ERR("open");
    free(filename);

    // block SIGUSR1
    sigset_t mask, old_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &old_mask) < 0)
        ERR("sigprocmask");

    // set handler for SIGUSR1
    sethandler(box_handler, SIGUSR1);

    // create buffer
    char* buf = malloc(9);
    if (buf == NULL)
        ERR("malloc");
    char* buf2 = malloc(5);
    if (buf2 == NULL)
        ERR("malloc");
    ssize_t count;
    int sum = 0;

    // suspend and wait for SIGUSR1
    while (1)
    {
        sigsuspend(&old_mask);
        if (last_sig == SIGUSR1)
        {
            // read data
            count = bulk_read(fd, buf, 8);
            if (count < 0)
                ERR("read");

            // move cursor
            if (lseek(fd, 0, SEEK_END) == -1)
                ERR("lseek");

            for (int i = 0; i < 4; i++)
                buf2[i] = buf[i + 4];

            buf[4] = '\0';
            buf2[4] = '\0';
            int pid_d = (int)strtol(buf, NULL, 10);
            int amount = (int)strtol(buf2, NULL, 10);
            sum += amount;

            printf("[%d] Obywatel %d wrzucił %d zł. Dziękuję! Łącznie zebrano: %d zł\n", getpid(), pid_d, amount, sum);
        }
    }

    free(buf);
    free(buf2);
    // close file
    if (TEMP_FAILURE_RETRY(close(fd)))
        ERR("close");
}

pid_t create_moneybox()
{
    pid_t pid = fork();

    switch (pid)
    {
        case 0:
            moneybox_work();
            exit(EXIT_SUCCESS);
        case -1:
            ERR("fork");
        default:
            return pid;
    }
}

void child_work(int* pids, int n)
{
    // block signals
    sigset_t mask, old_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGPIPE);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, &old_mask) < 0)
        ERR("sigprocmask");
    // set handler
    sethandler(child_handler, SIGUSR1);
    sethandler(child_handler, SIGUSR2);
    sethandler(child_handler, SIGPIPE);
    sethandler(child_handler, SIGINT);
    // wait for signal
    sigsuspend(&old_mask);
    int box_nr = -1;
    switch (last_sig)
    {
        case (SIGUSR1):
            box_nr = 0;
            break;
        case (SIGUSR2):
            box_nr = 1;
            break;
        case (SIGPIPE):
            box_nr = 2;
            break;
        case (SIGINT):
            box_nr = 3;
            break;
        default:
            break;
    }
    printf("[%d] Skierowany do skarbony nr %d\n", getpid(), box_nr);
    if (box_nr >= n)
    {
        printf("[%d] Nic tu nie ma, wracam do domu!\n", getpid());
        exit(EXIT_SUCCESS);
    }
    // get filename and open file
    char* filename = get_filename(pids[box_nr]);
    int fd = TEMP_FAILURE_RETRY(open(filename, O_WRONLY | O_APPEND));
    if (fd < 0)
        ERR("open");

    // get random money amount
    srand(getpid());
    int pid_d = getpid();
    int amount = rand() % 1901 + 100;

    // create buffer
    char* buf = malloc(9);
    if (buf == NULL)
        ERR("malloc");

    // write bytes to buf
    buf[0] = (char)((pid_d >> 24) & 0xFF);
    buf[1] = (char)((pid_d >> 16) & 0xFF);
    buf[2] = (char)((pid_d >> 8) & 0xFF);
    buf[3] = (char)(pid_d & 0xFF);
    buf[4] = (char)((amount >> 24) & 0xFF);
    buf[5] = (char)((amount >> 16) & 0xFF);
    buf[6] = (char)((amount >> 8) & 0xFF);
    buf[7] = (char)(amount & 0xFF);
    buf[8] = '\0';

    // write PID and amount to file
    sprintf(buf, "%d", getpid());
    ssize_t count = bulk_write(fd, buf, 8);
    if (count < 0)
        ERR("write");

    // close file and free memory
    if (TEMP_FAILURE_RETRY(close(fd)))
        ERR("close");
    free(filename);
    free(buf);

    // send signal to moneybox process
    printf("[%d] Wrzucam %d zł\n", getpid(), amount);
    if (kill(pids[box_nr], SIGUSR1))
        ERR("kill");
}

pid_t create_child(int* pids, int n)
{
    pid_t pid = fork();

    switch (pid)
    {
        case 0:
            child_work(pids, n);
            exit(EXIT_SUCCESS);
        case -1:
            ERR("fork");
        default:
            return pid;
    }
}

int main(int argc, char* argv[])
{
    // check arguments
    if (argc != 2)
        usage(argc, argv);

    int n = (int)strtol(argv[1], NULL, 10);
    if (n < 1 || n > 4)
        usage(argc, argv);

    // create n moneyboxes
    pid_t pids[n];
    for (int i = 0; i < n; i++)
    {
        pids[i] = create_moneybox();
    }

    int sigs[4] = {SIGUSR1, SIGUSR2, SIGPIPE, SIGINT};
    sethandler(SIG_IGN, SIGUSR1);
    sethandler(SIG_IGN, SIGUSR2);
    sethandler(SIG_IGN, SIGPIPE);
    sethandler(SIG_IGN, SIGINT);
    srand(time(NULL));

    // create ITER_COUNT children
    for (int i = 0; i < ITER_COUNT; i++)
    {
        pid_t pid = create_child(pids, n);
        ms_sleep(100);
        int signal_id = rand() % 4;
        if (kill(pid, sigs[signal_id]))
            ERR("kill");
        if (waitpid(pid, NULL, 0) < 0)
            ERR("waitpid");
    }

    // send SIGTERM to moneyboxes
    for (int i = 0; i < n; i++)
    {
        kill(pids[i], SIGTERM);
    }

    // wait for moneyboxes
    while (wait(NULL) > 0)
    {
    }

    printf("Zbiórka zakończona!\n");
    return EXIT_SUCCESS;
}
