#include "risk.h"

typedef unsigned int UINT;
typedef struct args
{
    region_t *board;
    int num_regions;
    int8_t owner;
    UINT seed;
    int *frustration_cnt;
    int *points;
    pthread_mutex_t *points_mtx;
    pthread_mutex_t *fr_mtx;
    pthread_mutex_t *board_mtx;
    sigset_t *mask;
} args_t;
typedef struct signal_args
{
    int num_regions;
    region_t *board;
    pthread_mutex_t *board_mtx;
    int *points;
    pthread_mutex_t *points_mtx;
    int *flag;
    pthread_mutex_t *flag_mtx;
} signal_args_t;

void usage(int argc, char **argv)
{
    fprintf(stderr, "USAGE: %s levelname.risk\n", argv[0]);
    exit(EXIT_FAILURE);
}

void print_board(region_t *board, int num_regions);
void *player(void *arg);
int change_field(int iregion, region_t *board, int8_t owner, pthread_mutex_t *board_mtx);
void block_board(pthread_mutex_t *board_mtx, int num_regions);
void unblock_board(pthread_mutex_t *board_mtx, int num_regions);
void init_board(region_t *board, int num_regions);
void *signal_handler(void *arg);

int main(int argc, char **argv)
{
    // check argc
    if (argc < 2)
        usage(argc, argv);

    // load regions from files
    int num_regions;
    region_t *board = load_regions(argv[1], &num_regions);

    // change random field to 'A' and 'B'
    srand(time(NULL));
    init_board(board, num_regions);
    printf("INITIAL BOARD STATE:\n");
    print_board(board, num_regions);

    // create args for A & B threads
    pthread_t tid[3];
    args_t args[2];
    int fr[2];
    pthread_mutex_t fr_mtx[2];

    // init board mtx
    pthread_mutex_t board_mtx[num_regions];
    for (int i = 0; i < num_regions; i++)
    {
        pthread_mutex_init(&board_mtx[i], NULL);
    }

    // init points mtx
    int points[2];
    pthread_mutex_t points_mtx[2];
    pthread_mutex_init(&points_mtx[0], NULL);
    pthread_mutex_init(&points_mtx[1], NULL);

    // signal handling:
    // block signals
    sigset_t mask;
    sigfillset(&mask);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // init args for 'A' & 'B'
    for (int i = 0; i < 2; i++)
    {
        args[i].num_regions = num_regions;
        args[i].board = board;
        args[i].seed = rand();
        fr[i] = 0;
        args[i].frustration_cnt = &fr[i];
        pthread_mutex_init(&fr_mtx[i], NULL);
        args[i].fr_mtx = &fr_mtx[i];
        args[i].board_mtx = board_mtx;
        points[i] = 0;
        args[i].points = &points[i];
        args[i].points_mtx = &points_mtx[i];
        args[i].mask = &mask;
        if (i == 0)
        {
            args[i].owner = 'A';
        }
        if (i == 1)
        {
            args[i].owner = 'B';
        }
    }

    // init args for signal handler
    signal_args_t *sargs = malloc(sizeof(signal_args_t));
    if (sargs == NULL)
        ERR("malloc");
    int stop_flag = 0;
    pthread_mutex_t flag_mtx;
    pthread_mutex_init(&flag_mtx, NULL);
    sargs->num_regions = num_regions;
    sargs->board = board;
    sargs->board_mtx = board_mtx;
    sargs->points = points;
    sargs->points_mtx = points_mtx;
    sargs->flag = &stop_flag;
    sargs->flag_mtx = &flag_mtx;

    // create signal thread
    if (pthread_create(&tid[2], NULL, signal_handler, sargs) != 0)
        ERR("pthread_create");

    // create 'A' and 'B' threads
    for (int i = 0; i < 2; i++)
    {
        if (pthread_create(&tid[i], NULL, player, &args[i]) != 0)
            ERR("pthread_create");
    }

    // main loop
    while (1)
    {
        // check if SIGTERM
        pthread_mutex_lock(&flag_mtx);
        if (stop_flag == 1)
        {
            pthread_mutex_unlock(&flag_mtx);
            // cancel threads
            for (int i = 0; i < 3; i++)
            {
                if (pthread_cancel(tid[i]) != 0)
                    ERR("pthread_cancel");
            }
            break;
        }
        pthread_mutex_unlock(&flag_mtx);

        // check if 'A' and 'B' are still playing
        pthread_mutex_lock(&fr_mtx[0]);
        pthread_mutex_lock(&fr_mtx[1]);
        if (fr[0] == FRUSTRATION_LIMIT && fr[1] == FRUSTRATION_LIMIT)
        {
            pthread_mutex_unlock(&fr_mtx[0]);
            pthread_mutex_unlock(&fr_mtx[1]);
            break;
        }
        pthread_mutex_unlock(&fr_mtx[0]);
        pthread_mutex_unlock(&fr_mtx[1]);

        // print board state
        ms_sleep(SHOW_MS);
        block_board(board_mtx, num_regions);
        print_board(board, num_regions);
        unblock_board(board_mtx, num_regions);
    }

    // cancel signal handler
    if (stop_flag == 0)
    {
        if (pthread_cancel(tid[2]) != 0)
            ERR("pthread_cancel");
    }

    // wait for 'A' and 'B' threads and signal thread
    for (int i = 0; i < 3; i++)
    {
        if (pthread_join(tid[i], NULL) != 0)
            ERR("pthread_join");
    }

    if (stop_flag == 0)
    {
        // print board state at the end
        printf("\nENDING BOARD STATE:\n");
        print_board(board, num_regions);

        // prints results
        printf("RESULTS:\n\tA: %d\n\tB: %d\n", points[0], points[1]);
    }
    else if (stop_flag == 1)
    {
        printf("RESULTS:\n\tROBIN HOOD WINS!\n");
        exit(EXIT_SUCCESS);
    }

    // cleanup
    for (int i = 0; i < num_regions; i++)
    {
        pthread_mutex_destroy(&board_mtx[i]);
    }
    pthread_mutex_destroy(&points_mtx[0]);
    pthread_mutex_destroy(&points_mtx[1]);
    pthread_mutex_destroy(&fr_mtx[0]);
    pthread_mutex_destroy(&fr_mtx[1]);
    pthread_mutex_destroy(&flag_mtx);

    // exit
    exit(EXIT_SUCCESS);
}

void print_board(region_t *board, int num_regions)
{
    for (int i = 0; i < num_regions; i++)
    {
        printf("%d [%c] : ", i, board[i].owner);
        for (int j = 0; j < board[i].num_neighbors; j++)
        {
            printf("%d", board[i].neighbors[j]);
            if (j < board[i].num_neighbors - 1)
                printf(";");
        }
        printf("\n");
    }
}

void *player(void *arg)
{
    args_t *args = (args_t *)arg;

    // block signals
    if (pthread_sigmask(SIG_BLOCK, args->mask, NULL) != 0)
        ERR("pthread_sigmask");

    int *fr_cnt = args->frustration_cnt;

    while (1)
    {
        ms_sleep(MOVE_MS);

        pthread_mutex_lock(args->fr_mtx);
        if (*fr_cnt == FRUSTRATION_LIMIT)
        {
            pthread_mutex_unlock(args->fr_mtx);
            return NULL;
        }
        pthread_mutex_unlock(args->fr_mtx);

        // get random field
        int field = rand_r(&args->seed) % args->num_regions;

        // try to change field
        if (change_field(field, args->board, args->owner, args->board_mtx) == -1)
        {
            pthread_mutex_lock(args->fr_mtx);
            (*fr_cnt) += 1;
            pthread_mutex_unlock(args->fr_mtx);
        }
        else
        {
            pthread_mutex_lock(args->fr_mtx);
            (*fr_cnt) = 0;
            pthread_mutex_unlock(args->fr_mtx);
            pthread_mutex_lock(args->points_mtx);
            (*args->points) += 1;
            pthread_mutex_unlock(args->points_mtx);
        }
    }
}

int change_field(int iregion, region_t *board, int8_t owner, pthread_mutex_t *board_mtx)
{
    // check if player already has this field
    pthread_mutex_lock(&board_mtx[iregion]);
    if (board[iregion].owner == owner)
    {
        printf("%c already has field %d.\n", owner, iregion);
        pthread_mutex_unlock(&board_mtx[iregion]);
        return -1;
    }

    // check if new field has neighbour that are owned by player
    int8_t *n = board[iregion].neighbors;
    int8_t size = board[iregion].num_neighbors;
    pthread_mutex_unlock(&board_mtx[iregion]);
    int flag = 0;

    // check neighbours
    for (int i = 0; i < size; i++)
    {
        pthread_mutex_lock(&board_mtx[n[i]]);
        if (board[n[i]].owner == owner)
        {
            flag = 1;
            pthread_mutex_unlock(&board_mtx[n[i]]);
            break;
        }
        pthread_mutex_unlock(&board_mtx[n[i]]);
    }

    // change owner or print msg
    switch (flag)
    {
        case (0):
            printf("%c doesn't have neighbours with field %d\n", owner, iregion);
            return -1;
        case (1):
            pthread_mutex_lock(&board_mtx[iregion]);
            board[iregion].owner = owner;
            pthread_mutex_unlock(&board_mtx[iregion]);
            return 0;
        default:
            return -1;
    }
}

void block_board(pthread_mutex_t *board_mtx, int num_regions)
{
    for (int i = 0; i < num_regions; i++)
    {
        pthread_mutex_lock(&board_mtx[i]);
    }
}

void unblock_board(pthread_mutex_t *board_mtx, int num_regions)
{
    for (int i = 0; i < num_regions; i++)
    {
        pthread_mutex_unlock(&board_mtx[i]);
    }
}

void init_board(region_t *board, int num_regions)
{
    // give random field to 'A'
    int field_A = rand() % num_regions;
    board[field_A].owner = 'A';

    // give random field to 'B'
    // make sure it's different field than 'A'
    int field_B = rand() % num_regions;
    while (field_B == field_A)
    {
        field_B = rand() % num_regions;
    }

    board[field_B].owner = 'B';
}

void *signal_handler(void *arg)
{
    signal_args_t *args = (signal_args_t *)arg;

    // mask for sigwait
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    int signo;

    for (;;)
    {
        if (sigwait(&mask, &signo))
            ERR("sigwait");

        switch (signo)
        {
            case SIGINT:
                // todo
                printf("TODO: SIGINT\n");
                break;
                ;
            case SIGTERM:
                // finish program
                pthread_mutex_lock(args->flag_mtx);
                (*args->flag) = 1;
                pthread_mutex_unlock(args->flag_mtx);
                return NULL;
            default:
                break;
        }
    }

    return NULL;
}
