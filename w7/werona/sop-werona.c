#include "l7-common.h"

volatile sig_atomic_t do_work = 1;

void usage(char* name)
{
    printf("%s <timeout>\n", name);
    printf("  timeout - max waiting time after receiving the last message/connection (in seconds)\n");
    exit(EXIT_FAILURE);
}

void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        ERR("fcntl GETFL");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        ERR("fcntl SETFL");
}

#define SWAP(a, b)                      \
    ({                                  \
        char c[sizeof(*(a))];           \
        memcpy(c, (a), sizeof(*(a)));   \
        memcpy((a), (b), sizeof(*(a))); \
        memcpy((b), (c), sizeof(*(a))); \
    })

#define MAX_CLIENTS 10
#define MAX_PAIRS 3
#define UNIX_SK_NAME "Laurenty"
#define MAX_MSG_LEN 63

typedef struct server_data {
    int local_socket;
    int epoll_fd;
    int timeout;
    int client_count;
    int client_fds[MAX_CLIENTS];
    char* lover_names[MAX_CLIENTS];
    char* client_names[MAX_CLIENTS];
} server_data_t;

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (0 == c)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
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

void add_to_epoll(int fd, server_data_t *data) {
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
        ERR("epoll_ctl");
}

void server_init(server_data_t *data) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        data->client_fds[i] = -1;
        data->client_names[i] = malloc(MAX_MSG_LEN * sizeof(char));
        data->lover_names[i] = malloc(MAX_MSG_LEN * sizeof(char));

        if (data->client_names[i] == NULL || data->lover_names[i] == NULL)
            ERR("malloc");

        strcpy(data->client_names[i], "\0");
        strcpy(data->lover_names[i], "\0");
    }

    data->client_count = 0;
    data->local_socket = bind_local_socket(UNIX_SK_NAME, MAX_CLIENTS);

    if ((data->epoll_fd = epoll_create1(0)) < 0)
        ERR("epoll_create1");

    add_to_epoll(data->local_socket, data);
}

void accept_new_client(server_data_t *data) {
    int new_client = add_new_client(data->local_socket);

    if (new_client < 0)
        return;

    if (data->client_count >= MAX_CLIENTS) {
        if (close(new_client) < 0)
            ERR("close");
        printf("Can't connect any more clients\n");
        return;
    }

    data->client_fds[data->client_count] = new_client;
    add_to_epoll(data->client_fds[data->client_count], data);
    data->client_count++;
    printf("Kolejna mloda osoba (%d) potrzebuje mojej pomocy!\n", new_client);
}

void close_client(server_data_t *data, int idx) {
    if (strcmp(data->client_names[idx], "\0") == 0) {
        printf("Utracilem kontakt z ??\n");
    }
    else if (strcmp(data->lover_names[idx], "\0") == 0) {
        printf("Utracilem kontakt z %s\n", data->client_names[idx]);
    }

    // remove client from epoll and close socket
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_DEL, data->client_fds[idx], NULL) < 0)
        ERR("epoll_ctl");
    if (close(data->client_fds[idx]) < 0)
        ERR("close");

    // reset values
    data->client_fds[idx] = -1;
    strcpy(data->client_names[idx], "\0");
    strcpy(data->lover_names[idx], "\0");

    // move other clients to the left in the buffers
    data->client_count--;
    for (int i = idx; i < data->client_count; i++) {
        SWAP(&data->client_fds[i], &data->client_fds[i+1]);
        SWAP(&data->lover_names[i], &data->lover_names[i+1]);
        SWAP(&data->client_names[i], &data->client_names[i+1]);
    }
}

void read_msg(server_data_t *data, int idx) {
    char buf[MAX_MSG_LEN + 1];
    ssize_t n;

    if ((n = bulk_read(data->client_fds[idx], buf, MAX_MSG_LEN)) < 0)
        ERR("bulk_read");

    // client disconnected
    if (n == 0) {
        close_client(data, idx);
        return;
    }

    // read client message
    buf[n] = '\0';

    // check if client name set
    int client_name_set = -1;
    if (strcmp(data->client_names[idx], "\0") == 0)
        client_name_set = 0;

    // check if lover name set
    int lover_name_set = -1;
    if (strcmp(data->lover_names[idx], "\0") == 0)
        lover_name_set = 0;

    // read msg char by char
    for (int i = 0; i < n; i++) {
        char c = buf[i];

        if (client_name_set >= 0) {
            if (c == ' ' || c == '\n') {
                data->client_names[idx][client_name_set] = '\0';
                client_name_set = -1;
                continue;
            }

            data->client_names[idx][client_name_set] = c;
            client_name_set++;
        }
        else if (lover_name_set >= 0) {
            if (c == ' ' || c == '\n') {
                data->lover_names[idx][lover_name_set] = '\0';
                lover_name_set = -1;
                continue;
            }

            data->lover_names[idx][lover_name_set] = c;
            lover_name_set++;
        }
        else {
            break;
        }
    }

    // check for unfinished msg
    if (client_name_set != -1 && client_name_set != 0) {
        data->client_names[idx][client_name_set] = '\0';
        client_name_set = -1;
    }
    if (lover_name_set != -1 && lover_name_set != 0) {
        data->lover_names[idx][lover_name_set] = '\0';
        lover_name_set = -1;
    }

    // both names set, close client
    if (client_name_set == -1 && lover_name_set == -1) {
        printf("%s chce pobrac sie z %s\n", data->client_names[idx], data->lover_names[idx]);
    }
}

void find_pairs(server_data_t *data) {

}

void server_work(server_data_t *data) {
    struct epoll_event events[MAX_CLIENTS];
    int nfds;

    while (1) {
        if ((nfds = epoll_wait(data->epoll_fd, events, MAX_CLIENTS, data->timeout)) > 0)
        {
            for (int i = 0; i < nfds; i++) {
                // new client waits for accepting
                if (events[i].data.fd == data->local_socket) {
                    accept_new_client(data);
                }
                // already accepted client sent a msg
                else {
                    // search which client
                    for (int j = data->client_count - 1; j >= 0; j--) {
                        if (events[i].data.fd == data->client_fds[j]) {
                            read_msg(data, j);
                            break;
                        }
                    }
                }
            }
        }
        // timeout
        else if (nfds == 0) {
            break;
        }
        // error
        else
        {
            if (errno == EINTR)
                continue;
            ERR("epoll_wait");
        }
    }

    // end server_work after timeout
    printf("Nikt juz nie potrzebuje mojej pomocy!\n");
}

void cleanup(server_data_t *data) {
    // free memory & close open sockets
    for (int i = 0; i < MAX_CLIENTS; i++) {
        free(data->client_names[i]);
        free(data->lover_names[i]);

        if (data->client_fds[i] != -1)
            close(data->client_fds[i]);
    }

    // close local socket
    if (close(data->local_socket) < 0)
        ERR("close");

    // unlink local socket from file system
    if (unlink(UNIX_SK_NAME) < 0)
        ERR("unlink");
}

int main(int argc, char** argv)
{
    // check arguments
    if (argc < 2)
        usage(argv[0]);

    // init server (local socket & epoll)
    server_data_t data;
    data.timeout = (int)strtol(argv[1], NULL, 10) * 1000;
    server_init(&data);

    // server work
    server_work(&data);

    // cleanup
    cleanup(&data);

    return EXIT_SUCCESS;
}
