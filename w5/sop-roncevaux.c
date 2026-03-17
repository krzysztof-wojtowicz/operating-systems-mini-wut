#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_KNIGHT_NAME_LENGHT 20

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

typedef struct knight
{
    char name[MAX_KNIGHT_NAME_LENGHT];
    int hp;
    int dmg;
} knight_t;

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void msleep(int millisec)
{
    struct timespec tt;
    tt.tv_sec = millisec / 1000;
    tt.tv_nsec = (millisec % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

int count_descriptors()
{
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL)
        ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do
    {
        errno = 0;
        if ((entry = readdir(dir)) != NULL)
        {
            if (lstat(entry->d_name, &stats))
                ERR("lstat");
            if (!S_ISDIR(stats.st_mode))
                count++;
        }
    } while (entry != NULL);
    if (chdir(path))
        ERR("chdir");
    if (closedir(dir))
        ERR("closedir");
    return count - 1;  // one descriptor for open directory
}

int open_file(FILE **file, char *path, char *who)
{
    // try to open path
    if ((*file = fopen(path, "r")) == NULL)
    {
        fprintf(stderr, "%s have not arrived on the battlefield\n", who);

        exit(EXIT_SUCCESS);
    }

    // read first line from file
    char *lineptr;
    size_t n;
    int count = -1;

    // get knights count
    if (getline(&lineptr, &n, *file) > 0)
    {
        count = (int)strtol(lineptr, NULL, 10);
    }

    free(lineptr);

    return count;
}

void read_knights(FILE *file, int n, knight_t knights[n])
{
    char *lineptr;
    size_t n_read;
    int i = 0;

    // read all lines from the file
    while ((getline(&lineptr, &n_read, file)) > 0)
    {
        sscanf(lineptr, "%s %d %d\n", knights[i].name, &knights[i].hp, &knights[i].dmg);

        i++;
    }

    free(lineptr);

    // close file
    if (fclose(file))
        ERR("fclose");
}

void create_pipes(int n, int r_pipes[n], int w_pipes[n])
{
    // temp fd to create pipe
    int temp_fd[2];

    // create n pipes
    for (int i = 0; i < n; i++)
    {
        if (pipe(temp_fd) < 0)
            ERR("pipe");

        r_pipes[i] = temp_fd[0];
        w_pipes[i] = temp_fd[1];
    }
}

void knight_work(knight_t knight, int enemies, int r_pipe, int w_pipes[enemies], char *who)
{
    srand(getpid());

    // print knight info
    printf("I am %s knight %s. I will serve my king with my %d HP and %d attack, desc: %d\n", who, knight.name, knight.hp, knight.dmg, count_descriptors());

    // set r_pipe to nonblock
    if (fcntl(r_pipe, F_SETFL, O_NONBLOCK) < 0)
        ERR("fcntl");

    int p = enemies - 1;    // number of alive enemies
    char dmg_taken;         // dmg taken from another knight
    int attack;             // attack sent to another knight
    int enemy;              // enemy knight index

    // attack loop
    while (1)
    {
        // read every byte from pipe
        while (read(r_pipe, &dmg_taken, 1) > 0)
        {
            knight.hp -= (int)dmg_taken;
            if (knight.hp < 0)
            {
                printf("%s dies.\n", knight.name);

                // close all fds
                if (close(r_pipe))
                    ERR("close pipe");
                for (int i = 0; i < enemies; i++)
                    if (close(w_pipes[i]))
                        ERR("close pipe");

                // exit after death
                exit(EXIT_SUCCESS);
            }
        }

        // EAGAIN means that read would've blocked
        if (errno != EAGAIN && errno != EPIPE)
            ERR("read");

        // get random enemy to attack and the random attack power
        enemy = rand() % (p + 1);
        attack = rand() % (knight.dmg+1);

        // send attack to enemy's pipe
        while (write(w_pipes[enemy], (char*)&attack, 1) < 0)
        {
            if (errno != EPIPE)
                ERR("write");

            // pipe closed -> number of alive enemies -= 1
            // if enemy was the last alive from array, then try with one before him
            if (enemy == p)
            {
                enemy--;
            }
            // else sent dead enemy to the end and swap him with the last alive
            else
            {
                int temp = w_pipes[p];
                w_pipes[p] = w_pipes[enemy];
                w_pipes[enemy] = temp;
            }

            // decrement p
            p--;

            // check if there are still alive knights left, if not -> exit
            if (p < 0)
            {
                printf("%s no enemies left.\n", knight.name);
                // close all fds
                if (close(r_pipe))
                    ERR("close pipe");
                for (int i = 0; i < enemies; i++)
                    if (close(w_pipes[i]))
                        ERR("close pipe");
                exit(EXIT_SUCCESS);
            }
        }

        // print info
        if (attack == 0)
            printf("%s attacks his enemy, however he deflected. [%d]\n", knight.name, attack);
        else if (attack <= 5)
            printf("%s goes to strike, he hit right and well. [%d]\n", knight.name, attack);
        else
            printf("%s strikes powerful blow, the shield he breaks and inflicts a big wound. [%d]\n", knight.name, attack);

        // wait random time from [1,10]
        int t = rand() % 10 + 1;
        msleep(t);
    }
}

void create_children(int n, knight_t knights[n], int r_pipes[n], int enemies, int w_pipes[enemies], char *who, int r_to_close[enemies], int w_to_close[n])
{
    // create n knights
    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();

        switch (pid)
        {
            case 0:
                // close all unused read descriptors for the other knights
                for (int j = 0; j < n; j++)
                    if (j != i && close(r_pipes[j]))
                        ERR("close pipe");

                // close unused write descriptors
                for (int j = 0; j < n; j++)
                    if (close(w_to_close[j]))
                        ERR("close pipe");

                // close unused read descriptors
                for (int j = 0; j < enemies; j++)
                    if (close(r_to_close[j]))
                        ERR("close pipe");

                // knight work
                knight_work(knights[i], enemies, r_pipes[i], w_pipes, who);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                break;
        }
    }
}

void create_knights(int sar_cnt, knight_t saracens[sar_cnt], int fra_cnt, knight_t franks[fra_cnt])
{
    // create pipes for Saracens & Franks
    int r_sar_pipes[sar_cnt];
    int w_sar_pipes[sar_cnt];
    int r_fra_pipes[fra_cnt];
    int w_fra_pipes[fra_cnt];
    create_pipes(sar_cnt, r_sar_pipes, w_sar_pipes);
    create_pipes(fra_cnt, r_fra_pipes, w_fra_pipes);

    // create saracen knights with write pipes to franks
    create_children(sar_cnt, saracens, r_sar_pipes, fra_cnt, w_fra_pipes, "Spanish", r_fra_pipes, w_sar_pipes);

    // create frank knights with write pipes to saracens
    create_children(fra_cnt, franks, r_fra_pipes, sar_cnt, w_sar_pipes, "Frankish", r_sar_pipes, w_fra_pipes);

    // close unused write descriptors
    for (int i = 0; i < sar_cnt; i++)
    {
        if (close(w_sar_pipes[i]))
            ERR("close pipe");
        if (close(r_sar_pipes[i]))
            ERR("close pipe");
    }

    for (int i = 0; i < fra_cnt; i++)
    {
        if (close(w_fra_pipes[i]))
            ERR("close pipe");
        if (close(r_fra_pipes[i]))
            ERR("close pipe");
    }

    printf("Parent descriptor count: %d\n", count_descriptors());
}

int main(int argc, char* argv[])
{
    //srand(time(NULL));

    // ignore SIPIPE
    if (set_handler(SIG_IGN, SIGPIPE))
        ERR("set handler");

    // open files and get knights count
    FILE *sar, *fra;
    int sar_cnt = open_file(&sar, "saraceni.txt", "Saracens");
    int fra_cnt = open_file(&fra, "franci.txt", "Franks");

    // read knights from files
    knight_t saracens[sar_cnt];
    knight_t franks[fra_cnt];
    read_knights(sar, sar_cnt, saracens);
    read_knights(fra, fra_cnt, franks);

    // create knights
    create_knights(sar_cnt, saracens, fra_cnt, franks);

    // wait for children
    while (wait(NULL) > 0) {}

    // exit
    printf("Battle ended, exiting...\n");
    exit(EXIT_SUCCESS);
}
