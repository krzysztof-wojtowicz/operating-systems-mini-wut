#include "l8_common.h"

#define SPELL_TYPES 3
const char* spell_names[SPELL_TYPES] = {"Divination", "Summon Elemental", "Fireball"};
#define BOARD_SIZE 8
#define BACKLOG 16

#define MAX_QUEUE 10
#define THREAD_COUNT 3
#define FAMILIAR_DELAY 100

#define MAX_CLIENTS 2
#define MAX_NAME_LENGTH 14

#define MAX_BUF 16

#define EMPTY '_'

union msg_body
{
    char chars[MAX_NAME_LENGTH + 1];
    uint16_t data[MAX_NAME_LENGTH / 2 + 1];
};

typedef struct cast
{
    int free;
    uint16_t spell_id;
    uint16_t x;
    uint16_t y;
    int player_id;
} cast_t;

typedef struct __attribute__((__packed__)) message {
    char type;
    char padding;
    union msg_body body;
} message_t;

typedef struct player
{
    char *name;
    struct sockaddr_in addr;
    uint16_t stones;
    pthread_mutex_t mtx;
} player_t;

typedef struct server_data
{
    // UDP socket & communication data
    uint16_t port;
    int socket_fd;
    struct sockaddr_in source_addr;
    int message_count;

    // fifo for spells
    pthread_mutex_t fifo_mtx;
    cast_t fifo[MAX_QUEUE];
    int start;
    int end;
    pthread_cond_t cv;

    // players data
    int player_count;
    player_t *players[2];

    // game data
    int game_running;
    pthread_mutex_t game_mtx;
    char board[BOARD_SIZE][BOARD_SIZE];
    pthread_mutex_t board_mtx;
} server_data_t;

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

int compare_addr(struct sockaddr_in addr1, struct sockaddr_in addr2)
{
    if (addr1.sin_port != addr2.sin_port || addr1.sin_addr.s_addr != addr2.sin_addr.s_addr)
        return 0;

    return 1;
}

void cleanup_player(player_t *player)
{
    free(player->name);

    if (pthread_mutex_destroy(&player->mtx) > 0)
        ERR("pthread_mutex_destroy");
}

int enqueue(server_data_t *data, cast_t *cast)
{
    pthread_mutex_lock(&data->fifo_mtx);

    // check if there is space
    if (data->fifo[data->end].free != 1)
    {
        pthread_mutex_unlock(&data->fifo_mtx);
        return -1;
    }

    // enqueue cast data
    data->fifo[data->end].free = 0;
    data->fifo[data->end].spell_id = cast->spell_id;
    data->fifo[data->end].x = cast->x;
    data->fifo[data->end].y = cast->y;

    // move end pointer
    data->end = (data->end + 1) % MAX_QUEUE;

    // signal that there is new element in queue
    pthread_cond_signal(&data->cv);

    pthread_mutex_unlock(&data->fifo_mtx);
    return 0;
}

int dequeue(server_data_t *data, cast_t *cast)
{
    // check if there are elements in fifo
    if (data->fifo[data->start].free == 1)
    {
        return -1;
    }

    // dequeue element
    cast->spell_id = data->fifo[data->start].spell_id;
    cast->x = data->fifo[data->start].x;
    cast->y = data->fifo[data->start].y;

    // change start position
    data->fifo[data->start].free = 1;
    data->start = (data->start + 1) % MAX_QUEUE;

    return 0;
}

void init_server(server_data_t *data, int argc, char** argv)
{
    // check arguments
    if (argc < 2)
        usage(argv[0]);

    // get port from argv
    errno = 0;
    data->port = (uint16_t)strtol(argv[1], NULL, 10);

    if (errno != 0)
        usage(argv[0]);

    // bind UDP socket
    data->socket_fd = bind_inet_socket(data->port, SOCK_DGRAM, -1);
    data->message_count = 0;

    // set timeout for socket
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(data->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        ERR("setsockopt");

    // prepare fifo queue
    pthread_mutex_init(&data->fifo_mtx, NULL);
    for (int i = 0; i < MAX_QUEUE; i++)
        data->fifo[i].free = 1;

    data->start = 0;
    data->end = 0;
    data->game_running = 1;
    pthread_mutex_init(&data->game_mtx, NULL);

    // initialize cond
    pthread_cond_init(&data->cv, NULL);

    // init game boards
    pthread_mutex_init(&data->board_mtx, NULL);

    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            data->board[i][j] = '_';
}

void* judge_work(void *arg)
{
    server_data_t *data = (server_data_t*)arg;

    while (1)
    {
        // sleep 1 second
        ms_sleep(1000);

        // wait for two players
        if (data->player_count < 2)
            continue;

        // lock mtxs before printing
        pthread_mutex_lock(&data->players[0]->mtx);
        pthread_mutex_lock(&data->players[1]->mtx);
        pthread_mutex_lock(&data->board_mtx);

        // print legend
        printf("\n[BOARD LEGEND]\n");
        printf(" 0 - elemental of %s [%d stones]\n", data->players[0]->name, data->players[0]->stones);
        printf(" 1 - elemental of %s [%d stones]\n\n", data->players[1]->name, data->players[1]->stones);

        // print board and count elementals
        uint16_t elementals1 = 0, elementals2 = 0;

        for (int i = 0; i < BOARD_SIZE + 2; i++)
            printf("-");
        printf("\n");
        for (int i = 0; i < BOARD_SIZE; i++)
        {
            printf("|");
            for (int j = 0; j < BOARD_SIZE; j++)
            {
                printf("%c", data->board[i][j]);
                if (data->board[i][j] == '0')
                    elementals1++;
                else if (data->board[i][j] == '1')
                    elementals2++;
            }
            printf("|\n");
        }
        for (int i = 0; i < BOARD_SIZE + 2; i++)
            printf("-");
        printf("\n");

        // update stones
        int sent_bytes;
        int delta1 = elementals1 / 2 - 1;
        int delta2 = elementals2 / 2 - 1;

        int new_stones1 = (int)data->players[0]->stones + delta1;
        int new_stones2 = (int)data->players[1]->stones + delta2;

        // check if player[s] lost
        int winner_id = -1;
        if (new_stones1 <= 0 && delta1 < 0 && new_stones2 <= 0 && delta2 < 0)
        {
            // both players lost -> first to login wins
            winner_id = 0;
        }
        if (new_stones1 <= 0 && delta1 < 0)
        {
            // second player wins
            winner_id = 1;
        }
        if (new_stones2 <= 0 && delta2 < 0)
        {
            // thirds player wins
            winner_id = 0;
        }

        // if there's a winner, print results and end game
        if (winner_id != -1)
        {
            printf("-= Congratulations, %s, you win! =-\n", data->players[winner_id]->name);

            // send datagrams to both players with results
            char win = 'w', lost = 'l';
            if ((sent_bytes = TEMP_FAILURE_RETRY(sendto(data->socket_fd, &win, sizeof(char), 0, (struct sockaddr *)&data->players[winner_id]->addr, sizeof(data->players[winner_id]->addr)))) < 0)
                ERR("sendto");
            if ((sent_bytes = TEMP_FAILURE_RETRY(sendto(data->socket_fd, &lost, sizeof(char), 0, (struct sockaddr *)&data->players[1 - winner_id]->addr, sizeof(data->players[1 - winner_id]->addr)))) < 0)
                ERR("sendto");

            pthread_mutex_unlock(&data->board_mtx);
            pthread_mutex_unlock(&data->players[1]->mtx);
            pthread_mutex_unlock(&data->players[0]->mtx);

            // end game
            pthread_mutex_lock(&data->game_mtx);
            data->game_running = 0;
            pthread_mutex_unlock(&data->game_mtx);

            pthread_cond_broadcast(&data->cv);
            return NULL;
        }

        // update stones
        data->players[0]->stones = (uint16_t)new_stones1;
        data->players[1]->stones = (uint16_t)new_stones2;

        uint16_t stones_net1 = htons(data->players[0]->stones);
        uint16_t stones_net2 = htons(data->players[1]->stones);

        // send changes to players
        if ((sent_bytes = TEMP_FAILURE_RETRY(sendto(data->socket_fd, &stones_net1, sizeof(uint16_t), 0, (struct sockaddr *)&data->players[0]->addr, sizeof(data->players[0]->addr)))) < 0)
            ERR("sendto");
        if ((sent_bytes = TEMP_FAILURE_RETRY(sendto(data->socket_fd, &stones_net2, sizeof(uint16_t), 0, (struct sockaddr *)&data->players[1]->addr, sizeof(data->players[1]->addr)))) < 0)
            ERR("sendto");

        // unlock mtxs afters printing
        pthread_mutex_unlock(&data->board_mtx);
        pthread_mutex_unlock(&data->players[1]->mtx);
        pthread_mutex_unlock(&data->players[0]->mtx);
    }

    return NULL;
}

void* thread_work(void *arg)
{
    server_data_t *data = (server_data_t*)arg;

    while (1)
    {
        // two conditions
        pthread_mutex_lock(&data->fifo_mtx);
        pthread_mutex_lock(&data->game_mtx);
        while (data->fifo[data->start].free == 1 && data->game_running == 1)
        {
            pthread_mutex_unlock(&data->game_mtx);
            pthread_cond_wait(&data->cv, &data->fifo_mtx);
            pthread_mutex_lock(&data->game_mtx);
        }

        // check if game is still running
        if (data->game_running == 0)
        {
            pthread_mutex_unlock(&data->game_mtx);
            pthread_mutex_unlock(&data->fifo_mtx);
            return NULL;
        }
        pthread_mutex_unlock(&data->game_mtx);

        // dequeue element
        cast_t cast;
        if (dequeue(data, &cast) < 0)
        {
            pthread_mutex_unlock(&data->fifo_mtx);
            printf("[Err] Fifo is empty!\n");
            ms_sleep(FAMILIAR_DELAY);
            continue;
        }

        pthread_mutex_unlock(&data->fifo_mtx);

        // calculate spell cost
        int cost = 0;
        switch (cast.spell_id)
        {
            case 0:
                cost = 1;
                break;
            case 1:
                cost = 3;
                break;
            case 2:
                cost = 4;
                break;
            default:
                break;
        }

        pthread_mutex_lock(&data->players[cast.player_id]->mtx);

        // check if player has enough stones
        if (cost > data->players[cast.player_id]->stones)
        {
            printf("[tee hee] Not enough pebbles, %s!\n", data->players[cast.player_id]->name);
        }
        else
        {
            data->players[cast.player_id]->stones -= cost;
            printf("[Cast] %s casts %s onto %d,%d\n", data->players[cast.player_id]->name, spell_names[cast.spell_id], cast.x, cast.y);

            // cast spell
            switch (cast.spell_id)
            {
                case 0:
                {
                    // calculate top-left corner of the 5x5 square
                    int left_x = cast.x - 2;
                    int top_y = cast.y - 2;

                    // set the buffer
                    uint16_t board_data[25];
                    int i = 0;

                    pthread_mutex_lock(&data->board_mtx);

                    for (int j = top_y; j < top_y + 5; j++)
                    {
                        for (int k = left_x; k < left_x + 5; k++)
                        {
                            // outside of the board
                            if (k < 0 || j < 0 || k >= BOARD_SIZE || j >= BOARD_SIZE)
                                board_data[i] = htons(3);
                            // empty
                            else if (data->board[j][k] == '_')
                                board_data[i] = htons(0);
                            // player's elemental
                            else if (data->board[j][k] == '0' + cast.player_id)
                                board_data[i] = htons(1);
                            // enemy's elemental
                            else
                                board_data[i] = htons(2);

                            i++;
                        }
                    }

                    // send datagram to player
                    int sent_bytes;
                    if ((sent_bytes = TEMP_FAILURE_RETRY(sendto(data->socket_fd, &board_data, 25 * sizeof(uint16_t), 0, (struct sockaddr *)&data->players[cast.player_id]->addr, sizeof(data->players[cast.player_id]->addr)))) < 0)
                        ERR("sendto");

                    pthread_mutex_unlock(&data->board_mtx);

                    break;
                }

                case 1:
                {
                    pthread_mutex_lock(&data->board_mtx);

                    // check if tile empty
                    if (data->board[cast.y][cast.x] == '_')
                    {
                        // spawn elemental
                        data->board[cast.y][cast.x] = '0' + cast.player_id;
                    }

                    pthread_mutex_unlock(&data->board_mtx);
                    break;
                }

                case 2:
                {
                    int left_x = cast.x - 1;
                    int top_y = cast.y - 1;

                    pthread_mutex_lock(&data->board_mtx);

                    for (int i = top_y; i < top_y + 3; i++)
                    {
                        for (int j = left_x; j < left_x + 3; j++)
                        {
                            if (i >= 0 && i < BOARD_SIZE && j >= 0 && j < BOARD_SIZE)
                            {
                                data->board[i][j] = '_';
                            }
                        }
                    }

                    pthread_mutex_unlock(&data->board_mtx);
                    break;
                }

                default:
                    break;
            }
        }

        uint16_t stones_net = htons(data->players[cast.player_id]->stones);

        // send datagram to player with current stones count
        int sent_bytes;
        if ((sent_bytes = TEMP_FAILURE_RETRY(sendto(data->socket_fd, &stones_net, sizeof(uint16_t), 0, (struct sockaddr *)&data->players[cast.player_id]->addr, sizeof(data->players[cast.player_id]->addr)))) < 0)
            ERR("sendto");

        pthread_mutex_unlock(&data->players[cast.player_id]->mtx);

        // wait FAMILIAR_DELAY ms
        ms_sleep(FAMILIAR_DELAY);
    }
}

void login_msg(message_t *msg, server_data_t *data)
{
    // check if new connection has unique ip + port pair
    if (data->player_count > 0 && compare_addr(data->source_addr, data->players[0]->addr) == 1)
    {
        printf("[Err] %s doesn't have unique ip+port pair\n", msg->body.chars);
        return;
    }

    // create new player
    player_t *new_player = malloc(sizeof(player_t));
    if (new_player == NULL)
        ERR("malloc");

    new_player->addr = data->source_addr;
    new_player->stones = 10;

    new_player->name = malloc(sizeof(char) * (strlen(msg->body.chars) + 1));
    strcpy(new_player->name, msg->body.chars);

    pthread_mutex_init(&new_player->mtx, NULL);

    // add player
    data->players[data->player_count] = new_player;
    data->player_count++;

    // print message
    printf("[Login] Welcome, %s\n", msg->body.chars);
    data->message_count++;
}

void cast_msg(message_t *msg, server_data_t *data, int player_id)
{
    if (player_id == -1)
        return;

    cast_t cast;
    cast.spell_id = msg->body.data[0];
    cast.x = msg->body.data[1];
    cast.y = msg->body.data[2];
    cast.player_id = player_id;

    // validate data
    if (cast.spell_id >= SPELL_TYPES)
    {
        printf("[Err] Invalid spell id %d\n", cast.spell_id);
        return;
    }
    if (cast.x >= BOARD_SIZE)
    {
        printf("[Err] Invalid x coordinate %d\n", cast.x);
        return;
    }
    if (cast.y >= BOARD_SIZE)
    {
        printf("[Err] Invalid y coordinate %d\n", cast.y);
        return;
    }

    // add cast to queue
    if (enqueue(data, &cast) < 0)
    {
        printf("[Err] Fifo is full!\n");
        return;
    }

    data->message_count++;
}

void quit_msg(message_t *msg, server_data_t *data, int player_id)
{
    if (player_id == -1)
        return;

    // print message
    printf("[Quit] %s quit. Goodbye!\n", data->players[player_id]->name);
    printf("-= Congratulations, %s, you win! =-\n", data->players[1 - player_id]->name);

    // cleanup after player
    cleanup_player(data->players[player_id]);
    free(data->players[player_id]);
    data->players[player_id] = NULL;
    data->player_count--;

    data->message_count++;

    pthread_mutex_lock(&data->game_mtx);
    data->game_running = 0;
    pthread_mutex_unlock(&data->game_mtx);

    pthread_cond_broadcast(&data->cv);
}

void server_work(server_data_t *data)
{
    message_t msg;

    while (1)
    {
        pthread_mutex_lock(&data->game_mtx);
        if (data->game_running == 0)
        {
            pthread_mutex_unlock(&data->game_mtx);
            return;
        }
        pthread_mutex_unlock(&data->game_mtx);

        socklen_t size = sizeof(data->source_addr);

        int received_bytes = recvfrom(data->socket_fd, &msg, MAX_BUF, 0, (struct sockaddr *)&data->source_addr, &size);

        // check errors
        if (received_bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            if (errno == EINTR)
                continue;
            ERR("recvfrom");
        }

        if (msg.type == 'l')
            msg.body.chars[received_bytes - 2] = 0;

        // at first, accept only login messages
        if (data->player_count < 2 && msg.type != 'l')
        {
            printf("[Err] Unaccepted message type: %c\n", msg.type);
            continue;
        }

        // after two players connected, accept only messages from them
        if (data->player_count == 2 && compare_addr(data->source_addr, data->players[0]->addr) == 0 && compare_addr(data->source_addr, data->players[1]->addr) == 0)
        {
            printf("[Err] Unknown player sent message\n");
            continue;
        }

        // check which player quit
        int player_id = -1;
        if (data->player_count == 2 && compare_addr(data->source_addr, data->players[0]->addr) == 1)
            player_id = 0;

        if (data->player_count == 2 && compare_addr(data->source_addr, data->players[1]->addr) == 1)
            player_id = 1;

        switch (msg.type)
        {
            case 'l':
                login_msg(&msg, data);
                break;
            case 'c':
                cast_msg(&msg, data, player_id);
                break;
            case 'q':
                quit_msg(&msg, data, player_id);
                break;
            default:
                printf("[Err] Invalid message type\n");
        }
    }
}

void cleanup(server_data_t *data)
{
    // close socket
    if (close(data->socket_fd) < 0)
        ERR("close");

    // destroy mtx
    if (pthread_mutex_destroy(&data->fifo_mtx) > 0)
        ERR("pthread_mutex_destroy");
    if (pthread_mutex_destroy(&data->board_mtx) > 0)
        ERR("pthread_mutex_destroy");
    if (pthread_mutex_destroy(&data->game_mtx) > 0)
        ERR("pthread_mutex_destroy");

    // destroy cond
    if (pthread_cond_destroy(&data->cv) > 0)
        ERR("pthread_cond_destroy");

    // free remaining players
    if (data->players[0] != NULL)
    {
        cleanup_player(data->players[0]);
        free(data->players[0]);
    }
    if (data->players[1] != NULL)
    {
        cleanup_player(data->players[1]);
        free(data->players[1]);
    }
}

void cleanup_threads(pthread_t *threads)
{
    for (int i = 0; i <= THREAD_COUNT; i++)
    {
        if (pthread_join(threads[i], NULL) > 0)
            ERR("pthread_join");
    }
}

int main(int argc, char** argv)
{
    // init server data struct
    server_data_t data;

    // init server (UDP socket, port)
    init_server(&data, argc, argv);

    // start thread workers
    pthread_t threads[THREAD_COUNT + 1];
    for (int i = 0; i < THREAD_COUNT; i++)
    {
        if (pthread_create(&threads[i], NULL, thread_work, &data) > 0)
            ERR("pthread_create");
    }
    if (pthread_create(&threads[THREAD_COUNT], NULL, judge_work, &data) > 0)
        ERR("pthread_create");

    // do server work
    server_work(&data);

    // cancel and join all threads
    cleanup_threads(threads);

    // cleanup
    cleanup(&data);

    exit(EXIT_SUCCESS);
}