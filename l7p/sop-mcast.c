#include "l7-common.h"

void usage(char* name)
{
    printf("%s <connect_port> <bind_port>\n", name);
    printf("  connect_port - port to receive messages from\n");
    printf("  bind_port - port to accept subscribers\n");
    exit(EXIT_FAILURE);
}

#define SWAP(a, b)                      \
    ({                                  \
        char c[sizeof(*(a))];           \
        memcpy(c, (a), sizeof(*(a)));   \
        memcpy((a), (b), sizeof(*(a))); \
        memcpy((b), (c), sizeof(*(a))); \
    })

#define MAX_CLIENTS 10
#define READ_BUFF_SIZE 64
#define GREP_LEN 5

typedef struct data
{
    int author_socket;
    int listen_socket;
    int client_count;
    int client_fds[MAX_CLIENTS + 2];
    char filters[MAX_CLIENTS + 1];
    char* messages[MAX_CLIENTS + 1];
    int msg_flag[MAX_CLIENTS + 1];
    int epoll_fd;
} data_t;

volatile sig_atomic_t last_signal = 0;

void default_handler(int sig) { last_signal = sig; }

void add_to_epoll(int epoll_fd, int socket)
{
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket, &event) == -1)
        ERR("epoll_ctl");
}

void epoll_init(data_t* data)
{
    data->epoll_fd = epoll_create1(0);
    if (data->epoll_fd < 0)
        ERR("epoll_create1");

    add_to_epoll(data->epoll_fd, data->author_socket);
    add_to_epoll(data->epoll_fd, data->listen_socket);
    add_to_epoll(data->epoll_fd, STDIN_FILENO);
}

void program_init(data_t* data, char** argv)
{
    // connect to author server
    data->author_socket = connect_tcp_socket("127.0.0.1", argv[1]);

    // bind listen socket
    uint16_t port = (uint16_t)strtol(argv[2], NULL, 10);
    data->listen_socket = bind_tcp_socket(port, MAX_CLIENTS);

    // stdin & stdout
    data->filters[0] = '\0';
    data->client_fds[0] = STDOUT_FILENO;
    data->client_fds[MAX_CLIENTS + 1] = STDIN_FILENO;

    // init client fds
    data->client_count = 0;
    for (int i = 1; i <= MAX_CLIENTS; i++)
    {
        data->client_fds[i] = -1;
        data->filters[i] = '\0';
    }

    // init messages buffers
    for (int i = 0; i <= MAX_CLIENTS; i++)
    {
        data->msg_flag[i] = 0;
        data->messages[i] = malloc(sizeof(char) * (GREP_LEN + 1));
        if (data->messages[i] == NULL)
            ERR("malloc");
        data->messages[i][0] = '\0';
    }
}

void close_client(data_t* data, int idx)
{
    // remove from epoll
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_DEL, data->client_fds[idx], NULL) < 0)
        ERR("epoll_ctl: del");

    // close socket
    if (close(data->client_fds[idx]) < 0)
        ERR("close");

    // move other clients data to the left in the arrays
    data->client_fds[idx] = -1;
    data->messages[idx][0] = '\0';
    data->filters[idx] = '\0';
    data->msg_flag[idx] = 0;

    for (int i = idx; i < data->client_count; i++)
    {
        SWAP(&data->client_fds[i], &data->client_fds[i + 1]);
        SWAP(&data->messages[i], &data->messages[i + 1]);
        SWAP(&data->filters[i], &data->filters[i + 1]);
        SWAP(&data->msg_flag[i], &data->msg_flag[i + 1]);
    }
    data->client_count--;
}

void send_msg(data_t* data, int idx, char* buf, int n)
{
    if (bulk_write(data->client_fds[idx], buf, n) < 0)
    {
        if (errno == EPIPE)
        {
            close_client(data, idx);
        }
        else
            ERR("bulk_write");
    }
}

void handle_message_send(data_t* data, int idx, char* buf, int n)
{
    // no filter -> send whole message
    if (data->filters[idx] == '\0')
    {
        send_msg(data, idx, buf, n);
        return;
    }

    // filter set -> search for sub strings of length GREP_LEN
    // read buffer char by char
    for (int i = 0; i < n; i++)
    {
        // current msg length
        int len = strlen(data->messages[idx]);

        // if msg full => send it and reset buffer
        if (len == GREP_LEN)
        {
            send_msg(data, idx, data->messages[idx], GREP_LEN);
            data->msg_flag[idx] = 0;
            data->messages[idx][0] = '\0';
            len = 0;
        }

        char msg_char = buf[i];
        // search for filter char
        if (data->msg_flag[idx] == 0 && msg_char == data->filters[idx])
        {
            // start of sub string, write first char to buffer & set flag
            data->msg_flag[idx] = 1;
        }

        // currently reading substring
        if (data->msg_flag[idx] == 1)
        {
            // move '\0' to the right & write char to buffer
            SWAP(&data->messages[idx][len], &data->messages[idx][len + 1]);
            data->messages[idx][len] = msg_char;
        }

        // else -> ignore char
    }
}

int read_from_author(data_t* data)
{
    // read from author
    char buf[READ_BUFF_SIZE + 1];
    ssize_t n;
    if ((n = read(data->author_socket, buf, READ_BUFF_SIZE)) < 0)
        ERR("read");

    // author closed connection
    if (n == 0)
        return -1;

    // write to all readers (including stdout at idx 0)
    for (int i = 0; i <= data->client_count; i++)
    {
        handle_message_send(data, i, buf, n);
    }

    return 0;
}

void accept_new_reader(data_t* data)
{
    int new_client = add_new_client(data->listen_socket);

    // maximum number of clients connected => close new connection
    if (data->client_count >= MAX_CLIENTS)
    {
        char* msg = "MAX_CLIENTS already connected\n";
        if (bulk_write(new_client, msg, strlen(msg)) < 0)
            ERR("bulk_write");
        if (close(new_client) < 0)
            ERR("close");
        return;
    }

    // add client to data & epoll
    data->client_fds[++data->client_count] = new_client;
    add_to_epoll(data->epoll_fd, new_client);
}

void read_from_reader(data_t* data, int idx)
{
    // read from reader
    char buf[READ_BUFF_SIZE + 1];
    ssize_t n;
    if ((n = read(data->client_fds[idx], buf, READ_BUFF_SIZE)) < 0)
        ERR("read");

    // reader closed connection
    if (n == 0 && idx != MAX_CLIENTS + 1)
        close_client(data, idx);

    // search for last alpha char
    char filter = '\0';
    for (int i = n - 1; i >= 0; i--)
    {
        if (isalpha(buf[i]))
        {
            filter = buf[i];
            break;
        }
    }

    // save filter to data struct
    if (filter != '\0')
    {
        // handle stdin & stdout pair
        if (idx == MAX_CLIENTS + 1)
            data->filters[0] = filter;
        // other clients
        else
            data->filters[idx] = filter;
    }
}

void program_work(data_t* data)
{
    struct epoll_event events[MAX_CLIENTS];
    int nfds;
    int is_running = 1;
    while (is_running && last_signal != SIGINT)
    {
        if ((nfds = epoll_wait(data->epoll_fd, events, MAX_CLIENTS, -1)) > 0)
        {
            for (int i = 0; i < nfds; i++)
            {
                int active_fd = events[i].data.fd;

                // read from author
                if (active_fd == data->author_socket)
                {
                    // if author disconnects => end program
                    if (read_from_author(data) < 0)
                        is_running = 0;
                }
                // accept new readers
                else if (active_fd == data->listen_socket)
                {
                    accept_new_reader(data);
                }
                // read from stdin
                else if (active_fd == STDIN_FILENO)
                {
                    read_from_reader(data, MAX_CLIENTS + 1);
                }
                // read from readers
                else
                {
                    // search which reader
                    for (int j = 0; j <= data->client_count; j++)
                    {
                        if (active_fd == data->client_fds[j])
                        {
                            read_from_reader(data, j);
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            if (errno == EINTR)
                continue;
            ERR("epoll_wait");
        }
    }
}

void cleanup(data_t* data)
{
    // close remaining client sockets
    for (int i = 1; i <= data->client_count; i++)
    {
        if (close(data->client_fds[i]) < 0)
            ERR("close");
    }

    // free allocated buffers
    for (int i = 0; i <= MAX_CLIENTS; i++)
    {
        free(data->messages[i]);
    }

    // close author socket
    if (close(data->author_socket) < 0)
        ERR("close author");

    // close epoll
    if (close(data->epoll_fd) < 0)
        ERR("close epoll");
}

int main(int argc, char** argv)
{
    // check argument count
    if (argc != 3)
    {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    // ignore SIGPIPE
    if (sethandler(SIG_IGN, SIGPIPE) < 0)
        ERR("sethandler");

    // set handler for SIGINT
    if (sethandler(default_handler, SIGINT) < 0)
        ERR("sethandler");

    // init program -> connect to author & init epoll
    data_t program_data;
    program_init(&program_data, argv);
    epoll_init(&program_data);

    // do program work (epoll loop)
    program_work(&program_data);

    // cleanup
    cleanup(&program_data);
    fprintf(stdout, "Nie daję już rady!!!\n");

    return EXIT_SUCCESS;
}
