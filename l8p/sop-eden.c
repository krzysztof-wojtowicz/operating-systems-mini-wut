#include "l8_common.h"

#define MSG_MAX 64
#define LOGIN_SIZE 16
#define COMMAND_SIZE 8
#define THREADS 8
#define PARAMS_SIZE 40

#define COMMANDS_COUNT 6

static char* COMMANDS[COMMANDS_COUNT] = {"RUN", "EXIT", "PAUSE", "COMPUTE", "LIST", "GATHER"};

typedef struct node
{
    struct node* next;
    char user[LOGIN_SIZE + 1];
    uint32_t samples;
    uint32_t seed;
} node_t;

typedef struct server_data
{
    uint16_t port;
    int socket_fd;
    struct sockaddr_in source_addr;

    node_t* head;
    node_t* tail;
    pthread_mutex_t list_mtx;
    pthread_cond_t cv;

    double pi[USERS];
    pthread_mutex_t pi_mtx[USERS];
} server_data_t;

typedef struct message
{
    char login[LOGIN_SIZE];
    char command[COMMAND_SIZE];
    char params[PARAMS_SIZE];
} message_t;

typedef union params
{
    char buffer[PARAMS_SIZE];
    uint32_t numbers[PARAMS_SIZE / 4];
} params_t;

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

void init_server(server_data_t* data, int argc, char** argv)
{
    // check argc
    if (argc < 2)
        usage(argv[0]);

    // get port
    data->port = (uint16_t)strtol(argv[1], NULL, 10);

    // bind socket
    data->socket_fd = bind_inet_socket(data->port, SOCK_DGRAM, -1);

    // init list
    data->head = NULL;
    data->tail = NULL;
    pthread_mutex_init(&data->list_mtx, NULL);
    pthread_cond_init(&data->cv, NULL);

    for (int i = 0; i < USERS; i++)
        pthread_mutex_init(&data->pi_mtx[i], NULL);
}

void add(server_data_t* data, uint32_t samples, uint32_t seed, char* user)
{
    pthread_mutex_lock(&data->list_mtx);

    if (data->head == NULL)
    {
        data->head = malloc(sizeof(node_t));
        data->head->samples = samples;
        data->head->seed = seed;
        strcpy(data->head->user, user);
        data->head->next = NULL;
        data->tail = data->head;

        pthread_mutex_unlock(&data->list_mtx);
        return;
    }

    data->tail->next = malloc(sizeof(node_t));
    data->tail = data->tail->next;
    data->tail->next = NULL;
    data->tail->samples = samples;
    data->tail->seed = seed;
    strcpy(data->tail->user, user);

    pthread_mutex_unlock(&data->list_mtx);
    pthread_cond_signal(&data->cv);
}

node_t* extract(server_data_t* data)
{
    pthread_mutex_lock(&data->list_mtx);

    node_t* p = data->head;
    data->head = data->head->next;

    if (data->head == NULL)
        data->tail = NULL;

    p->next = NULL;

    pthread_mutex_unlock(&data->list_mtx);

    return p;
}

void print_list(server_data_t* data)
{
    node_t* p = data->head;

    while (p)
    {
        printf("[user: %s, samples: %d, seed: %d] ", p->user, p->samples, p->seed);
        p = p->next;
    }

    printf("\n");
}

void free_list(server_data_t* data)
{
    node_t* p = data->head;

    while (p)
    {
        node_t* to_free = p;
        p = p->next;

        free(to_free);
    }
}

void send_list(server_data_t* data, char* login)
{
    pthread_mutex_lock(&data->list_mtx);

    // init buffer
    int sent_bytes;
    uint32_t buf[MSG_MAX / 4];
    for (int i = 0; i < MSG_MAX / 4; i++)
        buf[i] = 0;

    // create addr
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = data->source_addr.sin_port + 1;
    addr.sin_addr.s_addr = data->source_addr.sin_addr.s_addr;

    int j = 0;
    node_t* p = data->head;
    while (p)
    {
        // buffer full -> sent message
        if (j >= MSG_MAX / 4)
        {
            if ((sent_bytes = TEMP_FAILURE_RETRY(
                     sendto(data->socket_fd, &buf, MSG_MAX, 0, (struct sockaddr*)&addr, sizeof(addr)))) < 0)
                ERR("sendto");

            j = 0;
        }

        // add to buffer if task is owned by user
        if (strcmp(login, p->user) == 0)
        {
            buf[j] = htonl(p->samples);
            buf[j + 1] = htonl(p->seed);
            j += 2;
        }

        p = p->next;
    }

    // if buffer not empty send
    if (j != 0)
    {
        if ((sent_bytes = TEMP_FAILURE_RETRY(
                 sendto(data->socket_fd, &buf, MSG_MAX, 0, (struct sockaddr*)&addr, sizeof(addr)))) < 0)
            ERR("sendto");
    }

    pthread_mutex_unlock(&data->list_mtx);
}

void* thread_work(void* arg)
{
    server_data_t* data = (server_data_t*)arg;

    while (1)
    {
        // wait when list is empty
        pthread_mutex_lock(&data->list_mtx);
        while (data->head == NULL)
        {
            pthread_cond_wait(&data->cv, &data->list_mtx);
        }

        // extract
        node_t* p = extract(data);

        pthread_mutex_unlock(&data->list_mtx);

        // compute pi
        int samples = p->samples;
        if (samples > 1000)
            samples = 1000;

        const int seed = p->seed;
        double pi = compute_pi(samples, &seed);

        // find user index
        int idx = 0;
        for (int i = 0; i < USERS; i++)
            if (strcmp(p->user, LOGINS[i]) == 0)
            {
                idx = i;
                break;
            }

        // update pi value
        pthread_mutex_lock(&data->pi_mtx[idx]);
        data->pi[idx] = (data->pi[idx] + pi) / 2;
        pthread_mutex_unlock(&data->pi_mtx[idx]);

        // if samples left, add to list again
        if (p->samples - samples > 0)
        {
            p->samples -= samples;
            add(data, p->samples, p->seed, p->user);
        }

        free(p);
    }
}

int handle_message(server_data_t* data, message_t* msg)
{
    // search for user
    int valid_user = 0;
    char user[LOGIN_SIZE + 1];
    memcpy(user, msg->login, LOGIN_SIZE);
    user[LOGIN_SIZE] = 0;

    for (int i = 0; i < USERS; i++)
    {
        if (strcmp(LOGINS[i], user) == 0)
        {
            valid_user = 1;
            break;
        }
    }

    // unknown user
    if (valid_user == 0)
    {
        printf("error: unknown user %s\n", user);
        return -1;
    }

    // search for command
    int valid_command = 0;
    char command[COMMAND_SIZE + 1];
    memcpy(command, msg->command, COMMAND_SIZE);
    command[COMMAND_SIZE] = 0;

    for (int i = 0; i < COMMANDS_COUNT; i++)
    {
        if (strcmp(COMMANDS[i], command) == 0)
        {
            valid_command = 1;
            break;
        }
    }

    // unknown command
    if (valid_command == 0)
    {
        printf("error: unknown command %s\n", command);
        return -1;
    }

    printf("%s: %s ", user, command);

    // get parameters if COMPUTE command
    if (strcmp(command, COMMANDS[3]) == 0)
    {
        // allocate buffer and reset it to 0
        params_t* params = malloc(sizeof(params_t));
        for (int i = 0; i < PARAMS_SIZE; i++)
        {
            params->buffer[i] = 0;
        }

        memcpy(params->buffer, msg->params, PARAMS_SIZE);
        for (int i = 0; i < PARAMS_SIZE / 4; i++)
        {
            params->numbers[i] = ntohl(params->numbers[i]);
        }

        // print & handle params
        for (int i = 0; i < PARAMS_SIZE / 4; i += 2)
        {
            if (params->numbers[i] != 0 && params->numbers[i + 1] != 0)
            {
                if (params->numbers[i] > 10000000)
                {
                    printf("[invalid samples] ");
                    continue;
                }

                // print & add to queue
                printf("%d %d ", params->numbers[i], params->numbers[i + 1]);
                add(data, params->numbers[i], params->numbers[i + 1], user);
            }
        }

        free(params);

        // reset msg params buffer
        for (int i = 0; i < PARAMS_SIZE; i++)
            msg->params[i] = 0;
    }
    printf("\n");

    // list
    if (strcmp(command, COMMANDS[4]) == 0)
        send_list(data, user);

    // exit
    if (strcmp(command, COMMANDS[1]) == 0)
        return 1;

    return 0;
}

void server_work(server_data_t* data)
{
    message_t msg;
    while (1)
    {
        int received_bytes;
        socklen_t size = sizeof(data->source_addr);

        if ((received_bytes = TEMP_FAILURE_RETRY(
                 recvfrom(data->socket_fd, &msg, MSG_MAX, 0, (struct sockaddr*)&data->source_addr, &size))) < 0)
            ERR("recvfrom");

        // check message length
        if (received_bytes < LOGIN_SIZE + COMMAND_SIZE || received_bytes > MSG_MAX)
        {
            printf("error: wrong message length %d\n", received_bytes);
            continue;
        }

        if (handle_message(data, &msg) == 1)
            return;
    }
}

void cleanup(server_data_t* data)
{
    // close socket
    if (close(data->socket_fd) < 0)
        ERR("close");

    // free memory
    free_list(data);

    // destroy mtx
    pthread_mutex_destroy(&data->list_mtx);

    pthread_cond_destroy(&data->cv);

    for (int i = 0; i < USERS; i++)
        pthread_mutex_destroy(&data->pi_mtx[i]);
}

int main(int argc, char** argv)
{
    // init server
    server_data_t data;
    init_server(&data, argc, argv);

    // start threads
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++)
    {
        if (pthread_create(&threads[i], NULL, thread_work, &data) > 0)
            ERR("pthread_create");
    }

    // do server work
    server_work(&data);

    // print list at the end
    print_list(&data);

    // cleanup threads
    for (int i = 0; i < THREADS; i++)
    {
        if (pthread_cancel(threads[i]) > 0)
            ERR("pthread_cancel");

        if (pthread_join(threads[i], NULL) > 0)
            ERR("pthread_join");
    }

    // print pi value for each user
    for (int i = 0; i < USERS; i++)
    {
        printf("%s -> pi = %f\n", LOGINS[i], data.pi[i]);
    }

    // cleanup
    cleanup(&data);

    return EXIT_SUCCESS;
}
