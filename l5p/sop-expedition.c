#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

#define MOTHERSHIP_FIFO "/tmp/mothership"
#define SUPPLIES_CAPACITY 4
#define MAX_M 36

typedef struct
{
    int id;        // 100, 99, ..., 1, 0
    int supplies;  // Current supplies
} expedition_t;

void usage(int argc, char* argv[])
{
    fprintf(stderr, "Usage: %s M\n", argv[0]);
    fprintf(stderr, "  M - size of the board (3 <= M <= 6)\n");
    exit(EXIT_FAILURE);
}

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

int count_descriptors()
{
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL)
        ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do
    {
        errno = 0;
        if ((entry = readdir(dir)) != NULL)
        {
            if (lstat(entry->d_name, &stats))
                ERR("lstat");
            if (!S_ISDIR(stats.st_mode))
                count++;
        }
    } while (entry != NULL);
    if (chdir(path))
        ERR("chdir");
    if (closedir(dir))
        ERR("closedir");
    return count - 1;  // one descriptor for open directory
}

void msleep(const int ms)
{
    struct timespec tt;
    tt.tv_sec = ms / 1000;
    tt.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void create_pipes(int n, int* r_pipes, int* w_pipes)
{
    int temp_fd[2];

    // create n pipes for each process
    for (int i = 0; i < n; i++)
    {
        if (pipe(temp_fd))
            ERR("pipe");

        r_pipes[i] = temp_fd[0];
        w_pipes[i] = temp_fd[1];
    }
}

void node_work(int x, int y, int k, int r_pipe, int w_pipes[4])
{
    printf("Node [%d] (%d, %d): Opened %d descriptors.\n", k, x, y, count_descriptors() - 3);

    // random init
    srand(getpid());
    int local_supplies = rand() % 3;

    // read data from r_pipe
    while (1)
    {
        expedition_t exp;

        if (read(r_pipe, &exp, sizeof(expedition_t)) < 0)
            ERR("read");

        if (exp.id == 0)
            break;

        printf("Expedition %d: arrived at node [%d] (%d, %d)\n", exp.id, k, x, y);

        // get supplies
        if (local_supplies > 0)
        {
            exp.supplies++;
            local_supplies--;
            if (exp.supplies > SUPPLIES_CAPACITY)
                exp.supplies = SUPPLIES_CAPACITY;

            printf("Expedition %d: Good stuff.\n", exp.id);
        }

        // move to the next neighbor
        if (exp.supplies > 0)
        {
            printf("Expedition %d: Tomorrow comes.\n", exp.id);
            exp.supplies--;

            msleep(200);

            // choose randomly next neighbor & send expedition
            int nei = rand() % 4;
            int idx = nei;

            do
            {
                if (write(w_pipes[idx], &exp, sizeof(expedition_t)) < 0)
                {
                    if (errno == EPIPE)
                    {
                        printf("Expedition %d: Collapsed node!\n", exp.id);
                        idx = (idx + 1) % 4;
                    }
                    else
                    {
                        ERR("write pipe");
                    }
                }
            } while (idx != nei);
        }
        // death
        else
        {
            printf("Expedition %d: For those who come after.\n", exp.id);
            local_supplies += 2;
        }

        // 5% chance to collapse
        if (rand() % 20 == 0)
        {
            printf("Node [%d] (%d, %d): Collapsing.\n", k, x, y);
            break;
        }
    }

    // close descriptors before exit
    if (close(r_pipe))
        ERR("close pipe");

    for (int i = 0; i < 4; i++)
        if (close(w_pipes[i]))
            ERR("close pipe");

    printf("Node [%d] exiting... (%d, %d): Opened %d descriptors.\n", k, x, y, count_descriptors() - 3);
}

void create_children(int M, int n, pid_t* pid, int* r_pipes, int* w_pipes)
{
    for (int k = 0; k < n; k++)
    {
        pid[k] = fork();

        switch (pid[k])
        {
            case 0:
                // close all other read descriptors
                for (int j = k + 1; j < n; j++)
                    if (close(r_pipes[j]))
                        ERR("close pipe");

                // calculate coordinates
                int x = k / M;
                int y = k % M;
                int nei[4];

                // north
                if (x == 0)
                    nei[0] = k + (M - 1) * M;
                else
                    nei[0] = k - M;

                // south
                if (x == M - 1)
                    nei[1] = k - (M - 1) * M;
                else
                    nei[1] = k + M;

                // west
                if (y == 0)
                    nei[2] = k + M - 1;
                else
                    nei[2] = k - 1;

                // east
                if (y == M - 1)
                    nei[3] = k - M + 1;
                else
                    nei[3] = k + 1;

                // save pipes to neighbors
                int nei_pipes[4];
                for (int j = 0; j < 4; j++)
                    nei_pipes[j] = w_pipes[nei[j]];

                // close other write pipes
                for (int j = 0; j < n; j++)
                    if (j != nei[0] && j != nei[1] && j != nei[2] && j != nei[3])
                        if (close(w_pipes[j]))
                            ERR("close pipe");

                node_work(x, y, k, r_pipes[k], nei_pipes);

                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                if (close(r_pipes[k]))
                    ERR("close pipe");
                break;
        }
    }
}

void parent_work(int M, int n, int* w_pipes)
{
    // create fifo
    if (mkfifo(MOTHERSHIP_FIFO, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP))
        if (errno != EEXIST)
            ERR("create fifo");

    // open fifo
    int fifo;
    if ((fifo = open(MOTHERSHIP_FIFO, O_RDONLY)) < 0)
        ERR("open fifo");

    int id = 100;

    // read from fifo
    char buf[PIPE_BUF];
    ssize_t count;
    do
    {
        if ((count = read(fifo, &buf, PIPE_BUF)) < 0)
            ERR("read fifo");

        if (count > 0)
        {
            char* next;
            char* ptr = buf;

            // read commands from lines
            while ((next = strchr(ptr, '\n')) != NULL)
            {
                next[0] = '\0';
                // printf("%s\n", ptr);
                //  create new expedition
                expedition_t exp;
                exp.id = id--;
                exp.supplies = SUPPLIES_CAPACITY;

                // read coordinates from line
                int x = -1;
                int y = -1;
                sscanf(ptr, "SPAWN %d %d", &x, &y);

                // send expedition
                if (x != -1 && y != -1 && write(w_pipes[x * M + y], &exp, sizeof(expedition_t)) < 0)
                    printf("Expedition %d: failed.\n", exp.id);

                ptr = next + 1;
            }
        }

    } while (count > 0);

    // send id=0 expedition to every node
    expedition_t exp0;
    exp0.id = 0;
    exp0.supplies = SUPPLIES_CAPACITY;
    for (int i = 0; i < n; i++)
    {
        // send expedition
        if (write(w_pipes[i], &exp0, sizeof(expedition_t)) < 0)
            ERR("write pipe");
    }
}

int main(int argc, char* argv[])
{
    // check arguments
    if (argc < 2)
        usage(argc, argv);

    // ignore SIGPIPE
    if (set_handler(SIG_IGN, SIGPIPE) < 0)
        ERR("set handler");

    // get & validate M from argv
    int M = (int)strtol(argv[1], NULL, 10);
    if (M < 3 || M > 6)
        usage(argc, argv);

    // number of children
    int n = M * M;

    // open pipes
    int r_pipes[MAX_M];
    int w_pipes[MAX_M];
    create_pipes(n, r_pipes, w_pipes);

    // create n children
    pid_t pid[MAX_M];
    create_children(M, n, pid, r_pipes, w_pipes);

    printf("Mothership: Opened %d descriptors.\n", count_descriptors() - 3);

    parent_work(M, n, w_pipes);

    while (wait(NULL) > 0)
    {
    }

    // close write pipes
    for (int i = 0; i < n; i++)
    {
        if (close(w_pipes[i]))
            ERR("close pipe");
    }

    return EXIT_SUCCESS;
}
