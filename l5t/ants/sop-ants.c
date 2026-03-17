#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_GRAPH_NODES 32
#define MAX_PATH_LENGTH (2 * MAX_GRAPH_NODES)

#define FIFO_NAME "/tmp/colony_fifo"

volatile sig_atomic_t last_signal;

typedef struct ant
{
    int id;
    int path[MAX_PATH_LENGTH];
    int path_length;
} ant_t;

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void sigint_handler(int sig)
{
    last_signal = sig;
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
            ERR("waitpid:");
        }
    }
}

void msleep(int ms)
{
    struct timespec tt;
    tt.tv_sec = ms / 1000;
    tt.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void usage(int argc, char* argv[])
{
    printf("%s graph start dest\n", argv[0]);
    printf("  graph - path to file containing colony graph\n");
    printf("  start - starting node index\n");
    printf("  dest - destination node index\n");
    exit(EXIT_FAILURE);
}

int get_graph(char* path, int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES])
{
    // init graph with zeros
    for (int i = 0; i < MAX_GRAPH_NODES; i++)
        for (int j = 0; j < MAX_GRAPH_NODES; j++)
            graph[i][j] = 0;

    // open file
    FILE* colony;
    if ((colony = fopen(path, "r")) == NULL)
        ERR("fopen");

    // read from file
    char* lineptr;
    size_t n;
    int nodes_count = -1;
    while (getline(&lineptr, &n, colony) > -1)
    {
        // get nodes count (first line in file)
        if (nodes_count < 0)
        {
            nodes_count = (int)strtol(lineptr, NULL, 10);
            continue;
        }

        // divide lineptr in the place of ' '
        char* space = strchr(lineptr, ' ');
        space[0] = '\0';
        space++;

        // add edge to the graph
        int from = (int)strtol(lineptr, NULL, 10);
        int to = (int)strtol(space, NULL, 10);

        graph[from][to] = 1;
    }

    // check for errors
    if (errno == EINVAL || errno == ENOMEM)
        ERR("getline");

    free(lineptr);

    // close file
    if (fclose(colony))
        ERR("fclose");

    return nodes_count;
}

void print_graph(int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES], int node_count)
{
    printf("NODES: %d \n", node_count);
    for (int i = 0; i < node_count; i++)
    {
        for (int j = 0; j < node_count; j++)
            printf("%d ", graph[i][j]);
        printf("\n");
    }
}

void child_work(int i, int neighbours[MAX_GRAPH_NODES], int node_count, int r_pipe, int w_pipes[node_count], int dest)
{
    // random init
    srand(getpid());

    // open fifo for writing
    int fifo;
    if ((fifo = open(FIFO_NAME, O_WRONLY)) < 0)
        ERR("fifo open");

    // print graph info
    printf("%d:", i);
    for (int j = 0; j < node_count; j++)
    {
        // if neighbour -> print
        if (neighbours[j])
            printf(" %d", j);
        // else close pipe
        else if (close(w_pipes[j]))
            ERR("close");
    }
    printf("\n");

    // read from pipe
    char buf[PIPE_BUF];
    ssize_t count;
    while (1)
    {
        if (last_signal == SIGINT)
        {
            if (close(r_pipe))
                ERR("close");

            if (close(fifo) < 0)
                ERR("close fifo");

            exit(EXIT_SUCCESS);
        }

        // read ant from pipe
        if ((count = read(r_pipe, buf, sizeof(ant_t))) > 0)
        {
            // get ant from buf
            ant_t* ant = (ant_t*)buf;

            // add this node to ant path
            ant->path[ant->path_length] = i;
            ant->path_length++;

            // check if this is destination
            if (dest == i)
            {
                printf("Ant %d: found food\n", ant->id);

                if (write(fifo, (void*)ant, sizeof(ant_t)) < 0)
                    ERR("write fifo");

                continue;
            }

            // choose randomly next neighbour
            int next = rand() % node_count;
            while (neighbours[next % node_count] == 0 && next < 2*node_count)
                next++;

            next = next % node_count;

            // check if neighbour exists && if ant path isn't max
            if (neighbours[next] == 0 || ant->path_length == MAX_PATH_LENGTH)
            {
                printf("Ant %d: got lost\n", ant->id);
                continue;
            }

            // send ant to next neighbour
            if (write(w_pipes[next], (void*)ant, sizeof(ant_t)) < 0)
            {
                printf("Ant %d: got lost\n", ant->id);
                continue;
            }

            // chance to collapse
            if (rand() % 50 == 0)
            {
                printf("Node %d: collapsed\n", i);
                if (close(r_pipe))
                    ERR("close pipe");

                if (close(fifo))
                    ERR("close fifo");

                exit(EXIT_SUCCESS);
            }

            // wait before next ant
            msleep(100);
        }

        // signal interruption
        if (errno == EINTR)
            continue;

        // pipe closed
        if (count <= 0)
            break;
    }

    // close pipe
    if (close(r_pipe))
        ERR("close pipe");

    // close fifo
    if (close(fifo))
        ERR("close fifo");
}

void parent_work(int w_pipe, int node_count, pid_t pid[node_count])
{
    // open fifo for reading
    int fifo;
    if ((fifo = open(FIFO_NAME, O_RDONLY | O_NONBLOCK)) < 0)
        ERR("open fifo");

    // ant loop
    int ant_id = 0;
    char buf[sizeof(ant_t)];
    ssize_t count;
    while (last_signal != SIGINT)
    {
        // sleep for 1000ms
        msleep(1000);

        // create new ant
        ant_t new_ant;
        new_ant.id = ant_id++;
        new_ant.path_length = 0;

        // send ant to start node
        if (write(w_pipe, (void*)&new_ant, sizeof(ant_t)) < 0)
        {
            // stop simulation
            for (int i = 0; i < node_count; i++)
                kill(pid[i], SIGINT);

            last_signal = SIGINT;
            break;
        }

        // read from fifo
        if ((count = read(fifo, buf, sizeof(ant_t))) < 0)
            if (errno != EAGAIN)
                ERR("fifo read");

        if (count > 0)
        {
            ant_t* fifo_ant = (ant_t*)buf;
            printf("Ant %d path:", fifo_ant->id);
            for (int i = 0; i < fifo_ant->path_length; i++)
                printf(" %d", fifo_ant->path[i]);
            printf("\n");
        }
    }

    // close fifo
    if (close(fifo) < 0)
        ERR("close fifo");
}

int main(int argc, char* argv[])
{
    // check arguments
    if (argc < 4)
        usage(argc, argv);

    // set handler for SIGINT
    if (set_handler(sigint_handler, SIGINT) < 0)
        ERR("set_handler");

    // set handler for SIGCHLD
    if (set_handler(sigchld_handler, SIGCHLD) < 0)
        ERR("set_handler");

    // ignore SIGPIPE
    if (set_handler(SIG_IGN, SIGPIPE))
        ERR("set_handler");

    // create fifo
    if (mkfifo(FIFO_NAME, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0)
        if (errno != EEXIST)
            ERR("create fifo");

    // get file path
    char* path = argv[1];
    int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES];

    // get graph from file
    int node_count = get_graph(path, graph);

    // get start & dest node
    int start = (int)strtol(argv[2], NULL, 10);
    int dest = (int)strtol(argv[3], NULL, 10);

    // create pipes for children
    int r_pipes[node_count];
    int w_pipes[node_count];
    int temp_fd[2];
    for (int i = 0; i < node_count; i++)
    {
        if (pipe(temp_fd) < 0)
            ERR("pipe");

        r_pipes[i] = temp_fd[0];
        w_pipes[i] = temp_fd[1];
    }

    // create child for each node
    pid_t pid[node_count];
    for (int i = 0; i < node_count; i++)
    {
        pid[i] = fork();

        switch (pid[i])
        {
            case 0:
                // close all unused reading descriptors
                for (int j = i+1; j < node_count; j++)
                {
                    if (close(r_pipes[j]))
                        ERR("close");
                }

                child_work(i, graph[i], node_count, r_pipes[i], w_pipes, dest);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                // close unused descriptors
                if (close(r_pipes[i]))
                    ERR("close");
                break;
        }
    }

    // close unused write descriptors
    for (int i = 0; i < node_count; i++)
        if (i != start && close(w_pipes[i]))
            ERR("close");

    // parent work
    parent_work(w_pipes[start], node_count, pid);

    // wait for children
    while (wait(NULL) > 0) {}

    // remove fifo
    if (unlink(FIFO_NAME) < 0)
        ERR("remove fifo");

    // exit
    printf("Colony exiting...\n");
    exit(EXIT_SUCCESS);
}
