#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHOP_FILENAME "./shop"
#define MIN_SHELVES 8
#define MAX_SHELVES 256
#define MIN_WORKERS 1
#define MAX_WORKERS 64

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

// shared data struct
typedef struct shared_data
{
    pthread_mutex_t mtx[MAX_SHELVES];
    int work;
    pthread_mutex_t work_mtx;
    int dead;
    pthread_mutex_t dead_mtx;
} shared_data_t;

// shop struct
typedef struct shop
{
    int* shelves;
    int m;
    int n;
    shared_data_t *shared_data;
} shop_t;

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n - number of items (shelves), %d <= n <= %d\n", MIN_SHELVES, MAX_SHELVES);
    fprintf(stderr, "\t  m - number of workers, %d <= m <= %d\n", MIN_WORKERS, MAX_WORKERS);
    exit(EXIT_FAILURE);
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

void swap(int* x, int* y)
{
    int tmp = *y;
    *y = *x;
    *x = tmp;
}

void shuffle(int* array, int n)
{
    for (int i = n - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        swap(&array[i], &array[j]);
    }
}

void print_array(int* array, int n)
{
    for (int i = 0; i < n; ++i)
    {
        printf("%3d ", array[i]);
    }
    printf("\n");
}

// initializes shelves in the shop_t struct
// shelves are stored in the SHOP_FILENAME using mmap
void init_shop(shop_t *shop)
{
    // calculate shelves size
    int shelves_size = shop->n * sizeof(int);

    // create file SHOP_FILENAME
    int shop_fd;
    if ((shop_fd = open(SHOP_FILENAME, O_CREAT | O_RDWR | O_TRUNC, 0600)) < 0)
        ERR("open");

    // truncate file
    if (ftruncate(shop_fd, shelves_size))
        ERR("ftruncate");

    // map file to memory
    if ((shop->shelves = (int*)mmap(NULL, shelves_size, PROT_READ | PROT_WRITE, MAP_SHARED, shop_fd, 0)) == MAP_FAILED)
        ERR("mmap");

    // close fd
    if (close(shop_fd))
        ERR("close");

    // init shop shelves in the mapped memory
    for (int i = 0; i < shop->n; i++)
    {
        shop->shelves[i] = i + 1;
    }
}

// initializes shared_data_t struct inside shop_t struct
// it uses MAP_ANONYMOUS and MAP_SHARED to share data between processes
void init_shared_data(shop_t *shop)
{
    // create shared memory for shared variables
    if ((shop->shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
        ERR("mmap");

    // initialize mutex attributes
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

    // initialize mutexes for the shelves
    for (int i = 0; i < shop->n; i++)
    {
        pthread_mutex_init(&shop->shared_data->mtx[i], &attr);
    }

    // initialize work variables
    shop->shared_data->work = 1;
    pthread_mutex_init(&shop->shared_data->work_mtx, &attr);

    // initialize dead variables
    shop->shared_data->dead = 0;
    pthread_mutex_init(&shop->shared_data->dead_mtx, &attr);
}

// lock robust mutex && check if owner died
// count dead workers, worker always dies with two mtx locked
// so the counter will count each death two times
void lock_robust_mutex(shop_t *shop, pthread_mutex_t *mtx, int aisleIdx)
{
    int error;

    // lock mutex and return value
    if ((error = pthread_mutex_lock(mtx)) != 0)
    {
        if (error == EOWNERDEAD)
        {
            // repair mutex
            pthread_mutex_consistent(mtx);

            printf("[%d] Found a dead body in aisle %d.\n", getpid(), aisleIdx);

            // increment dead counter
            pthread_mutex_lock(&shop->shared_data->dead_mtx);
            shop->shared_data->dead++;
            pthread_mutex_unlock(&shop->shared_data->dead_mtx);
        }
        // another error
        else
            ERR("pthread_mutex_lock");
    }
}

// night worker loop
void night_worker(shop_t *shop)
{
    printf("[%d] Worker reports for a night shift.\n", getpid());
    srand(getpid());

    int id1, id2;

    while (1)
    {
        // check if there is still work to do
        pthread_mutex_lock(&shop->shared_data->work_mtx);
        if (shop->shared_data->work == 0)
        {
            printf("[%d] Worker leaving work.\n", getpid());
            pthread_mutex_unlock(&shop->shared_data->work_mtx);
            break;
        }
        pthread_mutex_unlock(&shop->shared_data->work_mtx);

        // get two different, random indexes
        do
        {
            id1 = rand() % shop->n;
            id2 = rand() % shop->n;
        } while (id1 == id2);

        // make sure id1 holds smaller index
        if (id1 > id2)
            swap(&id1, &id2);

        // lock mutexes in ascending order (TO PREVENT DEAD LOCK)
        // also count dead workers
        lock_robust_mutex(shop, &shop->shared_data->mtx[id1], id1);
        lock_robust_mutex(shop, &shop->shared_data->mtx[id2], id2);

        // if value on smaller idx is bigger -> swap them
        if (shop->shelves[id1] > shop->shelves[id2])
        {
            ms_sleep(100);

            // 1% chance to die
            if (rand() % 100 == 0)
            {
                printf("[%d] Trips over a pallet and dies.\n", getpid());
                abort();
            }

            swap(&shop->shelves[id1], &shop->shelves[id2]);
        }

        // unlock mutexes in descending order
        pthread_mutex_unlock(&shop->shared_data->mtx[id2]);
        pthread_mutex_unlock(&shop->shared_data->mtx[id1]);
    }
}

// manager worker loop
void manager_worker(shop_t *shop)
{
    printf("[%d] Manager reports for a night shift.\n", getpid());

    // manager logic
    int sorted = 0;
    while (1)
    {
        // lock mutexes in ascending order
        for (int i = 0; i < shop->n; i++)
            lock_robust_mutex(shop, &shop->shared_data->mtx[i], i);

        // print current shelves state
        printf("[%d] Current shelves state:\n", getpid());
        print_array(shop->shelves, shop->n);

        // check if there are still workers alive and print info
        pthread_mutex_lock(&shop->shared_data->dead_mtx);
        int alive = shop->m - shop->shared_data->dead/2;
        if (alive <= 0)
        {
            printf("[%d] All workers died, I hate my job.\n", getpid());
            break;
        }
        printf("[%d] Workers alive: %d.\n", getpid(), alive);
        pthread_mutex_unlock(&shop->shared_data->dead_mtx);

        // sync changes to file
        if (msync(shop->shelves, shop->n * sizeof(int), MS_SYNC) < 0)
            ERR("msync");

        // check if shelves are sorted
        for (int i = 0; i < shop->n; i++)
        {
            if (shop->shelves[i] != i + 1)
            {
                sorted = 0;
                break;
            }
            else
                sorted = 1;
        }

        // unlock mutexes in descending order
        for (int i = shop->n-1; i >= 0; i--)
            pthread_mutex_unlock(&shop->shared_data->mtx[i]);

        // if shelves are sorted, then let workers know that the work is done
        if (sorted)
        {
            pthread_mutex_lock(&shop->shared_data->work_mtx);
            shop->shared_data->work = 0;
            pthread_mutex_unlock(&shop->shared_data->work_mtx);
            printf("[%d] The shop shelves are sorted.\n", getpid());
            break;
        }

        // sleep for half a second
        ms_sleep(500);
    }
}

// creates M night workers and manager
void create_workers(shop_t* shop)
{
    // create M night workers
    pid_t pid[MAX_WORKERS+1];
    for (int i = 1; i < shop->m+1; i++)
    {
        pid[i] = fork();

        switch (pid[i])
        {
            case 0:
                night_worker(shop);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                break;
        }
    }

    // create manager worker
    pid[0] = fork();

    switch (pid[0])
    {
        case 0:
            manager_worker(shop);
            exit(EXIT_SUCCESS);
        case -1:
            ERR("fork");
        default:
            break;
    }
}

int main(int argc, char** argv)
{
    // check & validate arguments
    if (argc < 3)
        usage(argv[0]);

    shop_t shop;
    shop.n = (int)strtol(argv[1], NULL, 10);
    shop.m = (int)strtol(argv[2], NULL, 10);

    if (shop.n < MIN_SHELVES || shop.n > MAX_SHELVES || shop.m < MIN_WORKERS || shop.m > MAX_WORKERS)
        usage(argv[0]);

    // open shop with initialized shelves
    init_shop(&shop);

    // create mapped memory with shared data
    init_shared_data(&shop);

    // shuffle & print array
    shuffle(shop.shelves, shop.n);
    print_array(shop.shelves, shop.n);

    // create night workers
    create_workers(&shop);

    // wait for workers
    while (wait(NULL) > 0)
    {
    }

    // print end of shift info
    printf("Night shift in Bitronka is over.\n");
    print_array(shop.shelves, shop.n);

    // unmap memory
    if (munmap(shop.shelves, shop.n * sizeof(int)))
        ERR("munmap");
    if (munmap(shop.shared_data, sizeof(shared_data_t)))
        ERR("munmap");

    return EXIT_SUCCESS;
}
