#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)             \
    (__extension__({                               \
        long int __result;                         \
        do                                         \
            __result = (long int)(expression);     \
        while (__result == -1L && errno == EINTR); \
        __result;                                  \
    }))
#endif

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define USERS 10

static char* LOGINS[USERS] = {"kaczmarskik", "jelowickif", "hermant",  "turs",     "krasowskip",
                              "larysaz",     "zygulas",    "homendaw", "galazkap", "jastrzebskaaaaaa"};

double compute_pi(const int count, const int* seed)
{
    unsigned int state = (unsigned int)(*seed);
    int inside = 0;

    for (int i = 0; i < count; i++)
    {
        double x = (double)rand_r(&state) / (double)RAND_MAX;
        double y = (double)rand_r(&state) / (double)RAND_MAX;

        if (x * x + y * y <= 1.0)
        {
            inside++;
        }
        struct timespec ts = {0, 10000L};
        nanosleep(&ts, NULL);
    }

    return 4.0 * (double)inside / (double)count;
}

void ms_sleep(unsigned int milli)
{
    struct timespec ts = {milli / 1000, (milli % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

int bind_inet_socket(uint16_t port, int type, int backlog)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, type);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (SOCK_STREAM == type)
        if (listen(socketfd, backlog) < 0)
            ERR("listen");
    return socketfd;
}
