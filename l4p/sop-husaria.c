#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define KAHLENBERG 3
#define RESTOCK_STATIONS 4
#define ATTACK_COUNT 3
#define ARTILLERY_SLEEP 400
#define BANNER_SLEEP 500
#define RESTOCK_SLEEP 1000

typedef struct banner_args
{
    unsigned int seed;
    pthread_t tid;
    sem_t *sem;
    int *condition;
    pthread_mutex_t *cv_mtx;
    pthread_cond_t *cv;
    pthread_barrier_t *barrier;
    int *enemy_hp;
    pthread_mutex_t *hp_mtx;
    sem_t *sem_lance;
    int *weather;
    pthread_mutex_t *weather_mtx;
    sigset_t *mask;
} banner_args_t;

typedef struct artillery_args
{
    unsigned int seed;
    pthread_t tid;
    pthread_barrier_t *barrier;
    int *enemy_hp;
    pthread_mutex_t *hp_mtx;
    int *condition;
    pthread_mutex_t *cv_mtx;
    pthread_cond_t *cv;
    sigset_t *mask;
} artillery_args_t;

typedef struct signal_args
{
    pthread_t tid;
    int *weather;
    pthread_mutex_t *weather_mtx;
    int *last_signal;
    sigset_t *mask;
    pthread_t *tids;
    int tid_count;
} signal_args_t;

void ms_sleep(unsigned int milli)
{
    struct timespec ts = {milli / 1000, (milli % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void usage(int argc, char *argv[])
{
    printf("%s N M\n", argv[0]);
    printf("\t10 <= N <= 20 - number of banner threads\n");
    printf("\t2 <= M <= 8 - number of artillery threads\n");
    exit(EXIT_FAILURE);
}

void cleanup(void *arg) { pthread_mutex_unlock((pthread_mutex_t *)arg); }

void cleanup_sem(void *arg)
{
    if (sem_post((sem_t *)arg) != 0)
        ERR("sem_post");
}

void *banner_work(void *arg)
{
    // get args
    banner_args_t *args = (banner_args_t *)arg;

    // block signals
    if (pthread_sigmask(SIG_BLOCK, args->mask, NULL) != 0)
        ERR("pthread_sigmask");

    // check sem
    if (sem_wait(args->sem) != 0)
        ERR("sem_wait");

    // travel for 80-120ms
    unsigned int time = rand_r(&args->seed) % 41 + 80;
    ms_sleep(time);

    // print message and post to sem
    fprintf(stdout, "CAVALRY %lu: IN POSITION\n", args->tid);
    if (sem_post(args->sem) != 0)
        ERR("sem_post");

    // wait for artillery
    pthread_cleanup_push(cleanup, args->cv_mtx);
    pthread_mutex_lock(args->cv_mtx);
    while (*(args->condition) == 0)
    {
        pthread_cond_wait(args->cv, args->cv_mtx);
    }
    // pthread_mutex_unlock(args->cv_mtx);
    pthread_cleanup_pop(1);

    // ready to charge!
    fprintf(stdout, "CAVALRY %lu: READY TO CHARGE\n", args->tid);

    // attack!
    int weather = 0;

    for (int i = 1; i <= ATTACK_COUNT; i++)
    {
        // wait for all banners
        int res = pthread_barrier_wait(args->barrier);

        // one thread prints CHARGE <NUM>
        if (res == PTHREAD_BARRIER_SERIAL_THREAD)
        {
            fprintf(stdout, "CHARGE %d\n", i);
        }

        // sleep 500ms and update enemy_hp
        ms_sleep(BANNER_SLEEP);

        // check weather flag
        pthread_cleanup_push(cleanup, args->weather_mtx);
        pthread_mutex_lock(args->weather_mtx);
        weather = *(args->weather);
        // pthread_mutex_unlock(args->weather_mtx);
        pthread_cleanup_pop(1);

        // weather is bad => 10% chance to miss
        int p = -1;
        if (weather == 1)
        {
            p = rand_r(&args->seed) % 10;
        }

        // 10% chance
        if (p != 0)
        {
            pthread_cleanup_push(cleanup, args->hp_mtx);
            pthread_mutex_lock(args->hp_mtx);
            *(args->enemy_hp) -= 1;
            // pthread_mutex_unlock(args->hp_mtx);
            pthread_cleanup_pop(1);
        }
        else
        {
            fprintf(stdout, "CAVALRY %lu: MISSED\n", args->tid);
        }

        // restock lances (check sem_lance & sleep 100ms)
        pthread_cleanup_push(cleanup_sem, args->sem_lance);
        if (sem_wait(args->sem_lance) != 0)
            ERR("sem_wait");
        ms_sleep(RESTOCK_SLEEP);
        fprintf(stdout, "CAVALRY %lu: LANCE RESTOCKED\n", args->tid);
        // if (sem_post(args->sem_lance) != 0)
        //     ERR("sem_post");
        pthread_cleanup_pop(1);
    }

    // end thread
    return NULL;
}

void *artillery_work(void *arg)
{
    // get args
    artillery_args_t *args = (artillery_args_t *)arg;

    // block signals
    if (pthread_sigmask(SIG_BLOCK, args->mask, NULL) != 0)
        ERR("pthread_sigmask");

    // shooting loop
    while (1)
    {
        // get damage and wait 400ms
        int dmg = rand_r(&args->seed) % 6 + 1;
        ms_sleep(ARTILLERY_SLEEP);

        // update enemy_hp
        pthread_cleanup_push(cleanup, args->hp_mtx);
        pthread_mutex_lock(args->hp_mtx);
        *(args->enemy_hp) -= dmg;
        // pthread_mutex_unlock(args->hp_mtx);
        pthread_cleanup_pop(1);

        // wait for other artillery threads
        int res = pthread_barrier_wait(args->barrier);

        // one thread prints enemy_hp and sends broadcast to banners if <50
        int break_flag = 0;
        if (res == PTHREAD_BARRIER_SERIAL_THREAD)
        {
            pthread_cleanup_push(cleanup, args->hp_mtx);
            pthread_mutex_lock(args->hp_mtx);
            fprintf(stdout, "ARTILLERY: ENEMY HP %d\n", *(args->enemy_hp));
            // check condition
            if (*(args->enemy_hp) < 50)
            {
                // send broadcast to banners
                pthread_cleanup_push(cleanup, args->cv_mtx);
                pthread_mutex_lock(args->cv_mtx);
                *(args->condition) = 1;
                pthread_cond_broadcast(args->cv);
                // pthread_mutex_unlock(args->cv_mtx);
                pthread_cleanup_pop(1);

                // break while loop
                // pthread_mutex_unlock(args->hp_mtx);
                // pthread_cleanup_pop(1);
                // break;
                break_flag = 1;
            }
            // pthread_mutex_unlock(args->hp_mtx);
            pthread_cleanup_pop(1);
            if (break_flag == 1)
                break;
        }

        // check condition in other threads
        pthread_cleanup_push(cleanup, args->hp_mtx);
        pthread_mutex_lock(args->hp_mtx);
        if (*(args->enemy_hp) < 50)
        {
            // pthread_mutex_unlock(args->hp_mtx);
            // break;
            break_flag = 1;
        }
        // pthread_mutex_unlock(args->hp_mtx);
        pthread_cleanup_pop(1);
        if (break_flag == 1)
            break;

        // wait for barrier serial thread
        pthread_barrier_wait(args->barrier);
    }

    // end thread
    return NULL;
}

void *signal_handler(void *arg)
{
    // get args
    signal_args_t *args = (signal_args_t *)arg;

    // sigwait loop
    int sig_no;

    for (;;)
    {
        if (sigwait(args->mask, &sig_no))
            ERR("sigwait");

        switch (sig_no)
        {
            case SIGINT:
                // end program => cancel all banner and artillery threads
                for (int i = 0; i < args->tid_count; i++)
                {
                    if (pthread_cancel(args->tids[i]) != 0)
                        ERR("pthread_cancel");
                }

                // end signal thread and let main thread handle cleanup
                *(args->last_signal) = SIGINT;
                return NULL;
            case SIGUSR1:
                // change weather
                pthread_cleanup_push(cleanup, args->weather_mtx);
                pthread_mutex_lock(args->weather_mtx);
                *(args->weather) = 1;
                // pthread_mutex_unlock(args->weather_mtx);
                pthread_cleanup_pop(1);

                // print message
                fprintf(stdout, "RAIN AND MUD IS SLOWING DOWN CHARGE\n");
                *(args->last_signal) = SIGUSR1;
                break;
            default:
                break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    // check arg count
    if (argc < 3)
        usage(argc, argv);

    // get N & M from argv
    int n = (int)strtol(argv[1], NULL, 10);
    int m = (int)strtol(argv[2], NULL, 10);
    if (n < 10 || n > 20 || m < 2 || m > 8)
        usage(argc, argv);

    // block signals in main thread
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // args for threads & srand for seeds
    banner_args_t banner_args[n];
    artillery_args_t artillery_args[m];
    srand(time(NULL));
    int condition = 0;
    int enemy_hp = 100;
    int weather = 0;
    int last_signal = 0;

    // semaphor init
    sem_t sem, sem_lance;
    if (sem_init(&sem, 0, KAHLENBERG) != 0)
        ERR("sem_init");
    if (sem_init(&sem_lance, 0, RESTOCK_STATIONS) != 0)
        ERR("sem_init");

    // barrier init
    pthread_barrier_t barrier, banners_barrier;
    if (pthread_barrier_init(&barrier, NULL, m) != 0)
        ERR("pthread_barrier_init");
    if (pthread_barrier_init(&banners_barrier, NULL, n) != 0)
        ERR("pthread_barrier_init");

    // cond init
    pthread_cond_t cv;
    if (pthread_cond_init(&cv, NULL) != 0)
        ERR("pthread_cond_init");

    // mutex init
    pthread_mutex_t cv_mtx, hp_mtx, weather_mtx;
    if (pthread_mutex_init(&cv_mtx, NULL) != 0)
        ERR("pthread_mutex_init");
    if (pthread_mutex_init(&hp_mtx, NULL) != 0)
        ERR("pthread_mutex_init");
    if (pthread_mutex_init(&weather_mtx, NULL) != 0)
        ERR("pthread_mutex_init");

    // create banner threads
    for (int i = 0; i < n; i++)
    {
        banner_args[i].seed = rand();
        banner_args[i].sem = &sem;
        banner_args[i].condition = &condition;
        banner_args[i].cv = &cv;
        banner_args[i].cv_mtx = &cv_mtx;
        banner_args[i].barrier = &banners_barrier;
        banner_args[i].enemy_hp = &enemy_hp;
        banner_args[i].hp_mtx = &hp_mtx;
        banner_args[i].sem_lance = &sem_lance;
        banner_args[i].weather = &weather;
        banner_args[i].weather_mtx = &weather_mtx;
        banner_args[i].mask = &mask;

        if (pthread_create(&banner_args[i].tid, NULL, banner_work, &banner_args[i]) != 0)
            ERR("pthread_create");
    }

    // create artillery threads
    for (int i = 0; i < m; i++)
    {
        artillery_args[i].barrier = &barrier;
        artillery_args[i].condition = &condition;
        artillery_args[i].seed = rand();
        artillery_args[i].enemy_hp = &enemy_hp;
        artillery_args[i].hp_mtx = &hp_mtx;
        artillery_args[i].cv = &cv;
        artillery_args[i].cv_mtx = &cv_mtx;
        artillery_args[i].mask = &mask;

        if (pthread_create(&artillery_args[i].tid, NULL, artillery_work, &artillery_args[i]) != 0)
            ERR("pthread_create");
    }

    // create tid array for signal thread
    pthread_t tids[n + m];
    for (int i = 0; i < n; i++)
        tids[i] = banner_args[i].tid;
    for (int i = n; i < n + m; i++)
        tids[i] = artillery_args[i - n].tid;

    // create args for signal thread
    signal_args_t signal_args;
    signal_args.weather = &weather;
    signal_args.weather_mtx = &weather_mtx;
    signal_args.mask = &mask;
    signal_args.last_signal = &last_signal;
    signal_args.tids = tids;
    signal_args.tid_count = n + m;

    // create signal thread
    if (pthread_create(&signal_args.tid, NULL, signal_handler, &signal_args) != 0)
        ERR("pthread_create");

    // join banner threads
    for (int i = 0; i < n; i++)
    {
        if (pthread_join(banner_args[i].tid, NULL) != 0)
            ERR("pthread_join");
    }

    // join artillery threads
    for (int i = 0; i < m; i++)
    {
        if (pthread_join(artillery_args[i].tid, NULL) != 0)
            ERR("pthread_join");
    }

    // cancel and join signal handling thread
    if (pthread_cancel(signal_args.tid) != 0)
        ERR("pthread_cancel");
    if (pthread_join(signal_args.tid, NULL) != 0)
        ERR("pthread_join");

    // print battle results or information about SIGINT
    if (last_signal == SIGINT)
    {
        fprintf(stdout, "BATTLE WAS INTERRUPTED BY SIGINT\n");
    }
    else
    {
        fprintf(stdout, "BATTLE ENDED. ENEMY HEALTH: %d\n", enemy_hp);
        if (enemy_hp <= 0)
            fprintf(stdout, "VENIMUS, VIDIMUS, DEUS VICIT!\n");
    }

    // cleanup
    if (sem_destroy(&sem) != 0)
        ERR("sem_destroy");
    if (sem_destroy(&sem_lance) != 0)
        ERR("sem_destroy");
    if (pthread_barrier_destroy(&barrier) != 0)
        ERR("pthread_barrier_destroy");
    if (pthread_barrier_destroy(&banners_barrier) != 0)
        ERR("pthread_barrier_destroy");
    if (pthread_cond_destroy(&cv) != 0)
        ERR("pthread_cond_destroy");
    if (pthread_mutex_destroy(&cv_mtx) != 0)
        ERR("pthread_mutex_destroy");
    if (pthread_mutex_destroy(&hp_mtx) != 0)
        ERR("pthread_mutex_destroy");
    if (pthread_mutex_destroy(&weather_mtx) != 0)
        ERR("pthread_mutex_destroy");

    // exit
    exit(EXIT_SUCCESS);
}
