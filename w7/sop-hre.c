#include "w7-common.h"
#include "pthread.h"

#define ELECTORS 7
#define CANDIDATES 3
#define MAX_EVENTS 10
#define MAX_MSG 100

volatile sig_atomic_t last_signal;

typedef struct server_data
{
    int epoll_fd;
    int server_socket;
    int elector_sockets[ELECTORS];
    char* electors[ELECTORS];
    int votes[ELECTORS];
    int udp_socket;
    struct sockaddr_in udp_target_addr;
    pthread_mutex_t mtx[ELECTORS];
} server_data_t;

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port_tcp port_udp\n", name);
    exit(EXIT_FAILURE);
}

void default_handler(int sig)
{
    last_signal = sig;
}

void init_electors(server_data_t* data)
{
    for (int i = 0; i < ELECTORS; i++)
    {
        data->elector_sockets[i] = -1;
        data->votes[i] = -1;
        pthread_mutex_init(&data->mtx[i], NULL);
    }

    data->electors[0] = "Moguncja";
    data->electors[1] = "Trewir";
    data->electors[2] = "Kolonia";
    data->electors[3] = "Czechy";
    data->electors[4] = "Palatynat";
    data->electors[5] = "Saksonia";
    data->electors[6] = "Brandenburgia";
}

void server_init(int argc, char** argv, server_data_t* data)
{
    // Check argc
    if (argc < 3)
        usage(argv[0]);

    // Get port for tcp
    uint16_t port = (uint16_t)strtol(argv[1], NULL, 10);

    // Create, bind & listen on a new TCP socket
    data->server_socket = bind_tcp_socket(port, ELECTORS);

    // Get port for udp
    port = (uint16_t)strtol(argv[2], NULL, 10);

    // Create udp socket
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        ERR("socket");

    data->udp_socket = sock;

    // Prepare udp address
    memset(&data->udp_target_addr, 0, sizeof(struct sockaddr_in));
    data->udp_target_addr.sin_family = AF_INET;
    data->udp_target_addr.sin_port = htons(port);
    data->udp_target_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

void add_to_epoll(int epoll_fd, int socket)
{
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket, &event) == -1)
        ERR("epoll_ctl: listen_sock");
}

int epoll_init(int tcp_socket)
{
    // Create epoll
    int epoll_fd;
    if ((epoll_fd = epoll_create1(0)) < 0)
        ERR("epoll_create1:");

    // Add tcp socket fd to epoll
    add_to_epoll(epoll_fd, tcp_socket);

    return epoll_fd;
}

void write_msg(int socket, char* msg)
{
    if (bulk_write(socket, msg, strlen(msg)) < 0)
    {
        if (errno == EPIPE && close(socket) < 0)
            ERR("close");

        ERR("bulk_write");
    }
}

void close_socket(server_data_t* data, int socket)
{
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_DEL, socket, NULL) < 0)
        ERR("epoll delete");
    if (close(socket) < 0)
        ERR("close");
}

int read_int(server_data_t* data, int socket, int elector_id)
{
    char buf;
    int res;
    if ((res = bulk_read(socket, &buf, 1)) < 0)
        ERR("recv");

    if (res == 0)
    {
        close_socket(data, socket);

        // Handle elector disconnect
        if (elector_id != -1)
        {
            fprintf(stdout, "Elector from %s disconnected\n", data->electors[elector_id]);
            data->elector_sockets[elector_id] = -1;
        }

        // Handle unknown client disconnect
        if (elector_id == -1)
            fprintf(stdout, "Unknown client disconnected\n");

        return -1;
    }

    if (buf == '\n')
        return -1;

    int val = buf - '0';

    return val;
}

void accept_client(server_data_t* data)
{
    int client_socket = add_new_client(data->server_socket);
    add_to_epoll(data->epoll_fd, client_socket);
}

void new_elector(server_data_t* data, int client_socket)
{
    // Read int from client and check for errors
    int num = read_int(data, client_socket, -1);
    if (num == -1)
        return;

    num--;

    // Invalid elector id -> close connection
    if (num < 0 || num >= ELECTORS)
    {
        write_msg(client_socket, "Invalid id\n");
        close_socket(data, client_socket);
        return;
    }

    // Check if elector already exists -> close connection
    if (data->elector_sockets[num] != -1)
    {
        write_msg(client_socket, "Elector already connected\n");
        close_socket(data, client_socket);
        return;
    }

    // Welcome new elector
    write_msg(client_socket, "Welcome elector of ");
    write_msg(client_socket, data->electors[num]);
    write_msg(client_socket, "!\n");

    data->elector_sockets[num] = client_socket;
    fprintf(stdout, "Elector from %s connected\n", data->electors[num]);
}

void old_elector(server_data_t* data, int elector_id)
{
    int client_socket = data->elector_sockets[elector_id];

    // Read int from client and check for errors
    int num = read_int(data, client_socket, elector_id);
    if (num == -1)
        return;

    // Validate vote must be from [1,3] -> [0,2]
    num--;
    if (num < 0 || num >= CANDIDATES)
    {
        write_msg(client_socket, "Invalid vote\n");
        return;
    }

    // Accept vote
    pthread_mutex_lock(&data->mtx[elector_id]);
    data->votes[elector_id] = num;
    pthread_mutex_unlock(&data->mtx[elector_id]);

    write_msg(client_socket, "Vote accepted\n");
}

void server_work(server_data_t* data)
{
    struct epoll_event events[MAX_EVENTS];
    int nfds;
    while (last_signal != SIGINT)
    {
        if ((nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1)) > 0)
        {
            for (int n = 0; n < nfds; n++)
            {
                int flag = 1;
                // Check if existing elector
                for (int i = 0; i < ELECTORS; i++)
                {
                    if (data->elector_sockets[i] == events[n].data.fd)
                    {
                        flag = 0;
                        old_elector(data, i);
                        break;
                    }
                }

                // Accept new client
                if (events[n].data.fd == data->server_socket && flag)
                {
                    accept_client(data);
                }
                // Read from new elector
                else if (flag)
                {
                    new_elector(data, events[n].data.fd);
                }
            }
        }
        else
        {
            if (errno == EINTR)
                continue;
            ERR("epoll_wait");
        }
    }display



}

void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

void send_udp_msg(int udp_socket, struct sockaddr_in* target_addr, char* msg)
{
    if (sendto(udp_socket, msg, strlen(msg), 0, (struct sockaddr*)target_addr, sizeof(*target_addr)) < 0)
        ERR("sendto");
}

void* udp_thread_work(void* arg)
{
    server_data_t* data = (server_data_t*)arg;

    while (last_signal != SIGINT)
    {
        ms_sleep(1000);

        for (int i = 0; i < ELECTORS; i++)
            pthread_mutex_lock(&data->mtx[i]);

        int votes[CANDIDATES];
        for (int i = 0; i < CANDIDATES; i++)
            votes[i] = 0;

        for (int i = 0; i < ELECTORS; i++)
        {
            if (data->votes[i] == -1)
                continue;

            votes[data->votes[i]]++;
        }

        send_udp_msg(data->udp_socket, &data->udp_target_addr, "CURRENT RESULTS\n");
        for (int i = 0; i < CANDIDATES; i++)
        {
            char buf[50];
            sprintf(buf, " CANDIDATE %d -> %d\n", i + 1, votes[i]);

            send_udp_msg(data->udp_socket, &data->udp_target_addr, buf);
        }

        for (int i = ELECTORS - 1; i >= 0; i--)
            pthread_mutex_unlock(&data->mtx[i]);
    }

    return NULL;
}

int main(int argc, char** argv)
{
    // Ignore SIGPIPE
    if (sethandler(SIG_IGN, SIGPIPE) < 0)
        ERR("sethandler");
    // Ignore SIGINT
    if (sethandler(SIG_IGN, SIGINT) < 0)
        ERR("sethandler");

    // Create server data struct
    server_data_t server_data;
    init_electors(&server_data);

    // Create new tcp socket for listening
    server_init(argc, argv, &server_data);

    // Init epoll
    server_data.epoll_fd = epoll_init(server_data.server_socket);

    // Start udp thread
    pthread_t udp_thread;
    if (pthread_create(&udp_thread, NULL, udp_thread_work, &server_data) > 0)
        ERR("pthread_create");

    // Set SIGINT handler
    if (sethandler(default_handler, SIGINT) < 0)
        ERR("sethandler");

    // Do work
    server_work(&server_data);

    // Cancel udp thread & join
    // if (pthread_cancel(udp_thread) > 0)
    //     ERR("pthread_cancel");
    if (pthread_join(udp_thread, NULL) > 0)
        ERR("pthread_join");

    // Calculate and print results
    int results[CANDIDATES];
    for (int i = 0; i < CANDIDATES; i++)
        results[i] = 0;

    for (int i = 0; i < ELECTORS; i++)
    {
        if (server_data.votes[i] == -1)
            continue;

        results[server_data.votes[i]]++;
    }
    printf("\nFINAL RESULTS\n");
    for (int i = 0; i < CANDIDATES; i++)
    {
        printf(" CANDIDATE %d -> %d\n", i + 1, results[i]);
    }

    // Cleanup
    for (int i = 0; i < ELECTORS; i++)
    {
        if (server_data.elector_sockets[i] != -1)
            close_socket(&server_data, server_data.elector_sockets[i]);

        pthread_mutex_destroy(&server_data.mtx[i]);
    }

    if (close(server_data.udp_socket) < 0)
        ERR("close");
    if (close(server_data.server_socket) < 0)
        ERR("close");
    if (close(server_data.epoll_fd) < 0)
        ERR("close");

    return EXIT_SUCCESS;
}
