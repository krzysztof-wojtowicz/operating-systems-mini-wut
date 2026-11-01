#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MIN_N 1
#define MAX_N 15
#define MIN_M 5
#define MAX_M 20

#define SEMAPHORE_NAME "/gate"
#define SHM_NAME "/houses"

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

typedef struct shared_data
{
    pthread_mutex_t houses_mtx[MAX_N];
    pthread_mutex_t empty_mtx;
    pthread_cond_t cv;
    int empty_houses;
} shared_data_t;

typedef struct houses
{
    int n;
    int m;
    shared_data_t* shared_data;
} houses_t;

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n - number of townhouses, %d <= n <= %d\n", MIN_N, MAX_N);
    fprintf(stderr, "\t  m - number of nobles, %d <= m <= %d\n", MIN_M, MAX_M);
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

void init_houses_mtx(houses_t* houses)
{
    // map anonymous memory
    if ((houses->shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,
                                    0)) == MAP_FAILED)
        ERR("mmap");

    // init mtx
    pthread_mutexattr_t mtx_attr;
    pthread_mutexattr_init(&mtx_attr);
    pthread_mutexattr_setrobust(&mtx_attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&mtx_attr, PTHREAD_PROCESS_SHARED);

    for (int i = 0; i < houses->n; i++)
        pthread_mutex_init(&houses->shared_data->houses_mtx[i], &mtx_attr);

    pthread_mutex_init(&houses->shared_data->empty_mtx, &mtx_attr);
    pthread_mutexattr_destroy(&mtx_attr);

    // init cv
    pthread_condattr_t cv_attr;
    pthread_condattr_init(&cv_attr);
    pthread_condattr_setpshared(&cv_attr, PTHREAD_PROCESS_SHARED);

    pthread_cond_init(&houses->shared_data->cv, &cv_attr);

    pthread_condattr_destroy(&cv_attr);

    // init empty houses counter
    houses->shared_data->empty_houses = houses->n;
}

void child_exit(houses_t* houses, sem_t* sem, int* destroyed)
{
    // unmap shared memory
    if (munmap(houses->shared_data, sizeof(shared_data_t)) < 0)
        ERR("munmap");

    // close named sem
    if (sem_close(sem) < 0)
        ERR("sem_close");

    // unmap named memory
    if (munmap(destroyed, houses->n * sizeof(int)) < 0)
        ERR("munmap");

    exit(EXIT_SUCCESS);
}

void child_work(houses_t* houses)
{
    srand(getpid());

    // open and map shm
    int shm_fd;
    int* destroyed;
    if ((shm_fd = shm_open(SHM_NAME, O_RDWR, 0666)) < 0)
        ERR("shm_open");
    if ((destroyed = mmap(NULL, houses->n * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
        ERR("mmap");
    if (close(shm_fd) < 0)
        ERR("close");

    // open/create named sem
    sem_t* sem;
    if ((sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 10)) == SEM_FAILED)
        ERR("sem_open");

    sem_wait(sem);

    while (1)
    {
        // loops through houses
        for (int i = 0; i < houses->n; i++)
        {
            // try to lock house
            int error;
            if ((error = pthread_mutex_trylock(&houses->shared_data->houses_mtx[i])) != 0)
            {
                // mutex locked
                if (error == EBUSY)
                    continue;
                // previous owner of this mutex aborted
                if (error == EOWNERDEAD)
                {
                    printf("[%d] Nie może tak być! Sam posprzątam dom %d!\n", getpid(), i);
                    pthread_mutex_consistent(&houses->shared_data->houses_mtx[i]);
                    destroyed[i] = 0;
                }
                else
                    ERR("pthread_mutex_trylock");
            }

            // decrement empty counter
            pthread_mutex_lock(&houses->shared_data->empty_mtx);
            houses->shared_data->empty_houses--;
            pthread_mutex_unlock(&houses->shared_data->empty_mtx);

            printf("[%d] Jakże komfortowy dom nr %d!\n", getpid(), i);

            // check if house is destroyed
            if (destroyed[i] == 1)
            {
                printf("[%d] Dom %d to ostatnia rzecz jaką zobaczę\n", getpid(), i);
                sem_post(sem);
                abort();
            }

            // 10% chance to destroy house
            if (rand() % 10 == 0)
            {
                // print randomly one of the three messages
                switch (rand() % 3)
                {
                    case 0:
                        printf("[%d] Czas wyburzyć ścianę w domu %d!\n", getpid(), i);
                        break;
                    case 1:
                        printf("[%d] W domu %d jest tyle miodu pitnego, wypiję wszystko!\n", getpid(), i);
                        break;
                    default:
                        printf("[%d] Co? Brak wanny w domu %d? Sam ją wykopię!\n", getpid(), i);
                        break;
                }

                destroyed[i] = 1;
            }

            ms_sleep(300);
            pthread_mutex_unlock(&houses->shared_data->houses_mtx[i]);

            // increment empty counter
            pthread_mutex_lock(&houses->shared_data->empty_mtx);
            houses->shared_data->empty_houses++;
            pthread_mutex_unlock(&houses->shared_data->empty_mtx);

            // signal cond variable
            pthread_cond_signal(&houses->shared_data->cv);

            // post to semaphore before exit
            sem_post(sem);

            child_exit(houses, sem, destroyed);
        }

        // wait on cond variable
        pthread_mutex_lock(&houses->shared_data->empty_mtx);
        while (houses->shared_data->empty_houses <= 0)
        {
            pthread_cond_wait(&houses->shared_data->cv, &houses->shared_data->empty_mtx);
        }
        pthread_mutex_unlock(&houses->shared_data->empty_mtx);

        printf("[%d] Spróbuję jeszcze raz\n", getpid());
    }

    // no empty house [FIRST STAGE]
    // printf("[%d] Po cóż tu przybyłem?\n", getpid());
    // child_exit(houses);
}

void create_children(houses_t* houses)
{
    // create m children
    pid_t pid[MAX_M];

    for (int i = 0; i < houses->m; i++)
    {
        pid[i] = fork();

        switch (pid[i])
        {
            case 0:
                child_work(houses);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                break;
        }
    }
}

void init_shm(houses_t* houses)
{
    size_t shm_size = houses->n * sizeof(int);

    // create shm
    int shm_fd;
    if ((shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666)) < 0)
        ERR("shm_open");

    // truncate shm object to the m * int size
    if (ftruncate(shm_fd, shm_size) < 0)
        ERR("ftruncate");

    // map shm to memory
    int* destroyed;
    if ((destroyed = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
        ERR("mmap");

    // initialize with zeros
    for (int i = 0; i < houses->n; i++)
    {
        destroyed[i] = 0;
    }

    // close fd and unmap memory
    if (close(shm_fd) < 0)
        ERR("close");
    if (munmap(destroyed, shm_size) < 0)
        ERR("munmap");
}

void print_shm(houses_t* houses)
{
    // open and map shm
    int shm_fd;
    int* destroyed;
    if ((shm_fd = shm_open(SHM_NAME, O_RDWR, 0666)) < 0)
        ERR("shm_open");
    if ((destroyed = mmap(NULL, houses->n * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
        ERR("mmap");
    if (close(shm_fd) < 0)
        ERR("close");

    // print shared memory data
    printf("[%d] Zniszczone domy: ", getpid());
    for (int i = 0; i < houses->n; i++)
        printf("%d ", destroyed[i]);
    printf("\n");

    // unmap shared memory
    if (munmap(destroyed, houses->n * sizeof(int)) < 0)
        ERR("munmap");

    // unlink shm from file system
    if (shm_unlink(SHM_NAME) < 0)
        ERR("shm_unlink");
}

int main(int argc, char** argv)
{
    // check arguments count
    if (argc < 3)
        usage(argv[0]);

    // get n & m from argv
    houses_t houses;
    houses.n = (int)strtol(argv[1], NULL, 10);
    houses.m = (int)strtol(argv[2], NULL, 10);

    // validate args
    if (houses.n < MIN_N || houses.n > MAX_N || houses.m < MIN_M || houses.m > MAX_M)
        usage(argv[0]);

    // make sure that named sem SEMAPHORE_NAME doesn't exist
    if (sem_unlink(SEMAPHORE_NAME) < 0)
    {
        if (errno != ENOENT)
            ERR("sem_unlink");
    }

    // delete named shm if exists
    if (shm_unlink(SHM_NAME) < 0)
    {
        if (errno != ENOENT)
            ERR("shm_unlink");
    }

    // init named shared memory
    init_shm(&houses);

    // init shared memory and houses mtx
    init_houses_mtx(&houses);

    // create m children
    create_children(&houses);

    // wait for children
    int status;
    while (wait(&status) > 0)
    {
        // if child process aborted, wake all children waiting on cond
        if (!WIFEXITED(status))
        {
            // increment empty houses counter
            pthread_mutex_lock(&houses.shared_data->empty_mtx);
            houses.shared_data->empty_houses++;
            pthread_mutex_unlock(&houses.shared_data->empty_mtx);

            // broadcast to cond variable
            pthread_cond_broadcast(&houses.shared_data->cv);
        }
    }

    // print shared memory data and unlink it from file system
    print_shm(&houses);

    // destroy house mtxes
    for (int i = 0; i < houses.n; i++)
        pthread_mutex_destroy(&houses.shared_data->houses_mtx[i]);

    // destroy other mtx and cond
    pthread_mutex_destroy(&houses.shared_data->empty_mtx);
    pthread_cond_destroy(&houses.shared_data->cv);

    // delete named sem
    if (sem_unlink(SEMAPHORE_NAME) < 0)
    {
        if (errno != ENOENT)
            ERR("sem_unlink");
    }

    // unmap shared memory
    if (munmap(houses.shared_data, sizeof(shared_data_t)) < 0)
        ERR("munmap");

    exit(EXIT_SUCCESS);
}
