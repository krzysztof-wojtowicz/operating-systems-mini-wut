#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef struct node {
    int num;
    struct node* next;
} node;

typedef struct args_t {
    pthread_t tid;
    int* size;
    node** head;
    int* stop_flag;
    pthread_mutex_t* stop_mtx;
    pthread_mutex_t* head_mtx;
} args_t;

void usage(char** argv) {
    fprintf(stderr, "%s takes one argument:\n\tk - max list element\n", argv[0]);
    exit(EXIT_FAILURE);
}

node* gen_list(int k);
void delete(int i, node** head);
void print_list(node* head);
void* thread_work(void* arg);

int main(int argc, char** argv) {
    // check args
    if (argc < 2)
        usage(argv);

    // get int from argv
    int k = (int)strtol(argv[1], NULL, 10);
    if (k <= 0)
        usage(argv);
    printf("[%lu] Hello from main, k = %d\n", pthread_self(), k);

    // block all signals in main thread
    sigset_t mask;
    sigfillset(&mask);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // generate list from 1 to k
    node* head = gen_list(k);

    // flag stop and args for thread
    int stop_flag = 0;
    args_t *args = malloc(sizeof(args_t));
    if (args == NULL)
        ERR("malloc");

    args->head = &head;
    args->size = &k;
    args->stop_flag = &stop_flag;

    // attr for thread
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // mtx for list and stop_flag
    pthread_mutex_t stop_mtx;
    pthread_mutex_init(&stop_mtx, NULL);
    args->stop_mtx = &stop_mtx;
    pthread_mutex_t head_mtx;
    pthread_mutex_init(&head_mtx, NULL);
    args->head_mtx = &head_mtx;

    // create signal thread
    if (pthread_create(&args->tid, &attr, thread_work, args) != 0)
        ERR("pthread_create");

    // loop
    while (1) {
        // check stop flag
        pthread_mutex_lock(&stop_mtx);
        if (stop_flag == 1) {
            pthread_mutex_unlock(&stop_mtx);
            break;
        }
        pthread_mutex_unlock(&stop_mtx);

        // print list
        pthread_mutex_lock(&head_mtx);
        print_list(head);
        pthread_mutex_unlock(&head_mtx);

        // sleep
        sleep(1);
    }

    // clean up before exit
    for (int i = 0; i < k; i++) {
        delete(0, &head);
    }
    pthread_mutex_destroy(&stop_mtx);
    pthread_mutex_destroy(&head_mtx);
    pthread_attr_destroy(&attr);
    exit(EXIT_SUCCESS);
}

node* gen_list(int k) {
    if (k < 1)
        return NULL;

    node* head = malloc(sizeof(node));
    if (head == NULL)
        ERR("malloc");

    head->num = 1;
    head->next = NULL;
    node* p = head;

    for (int i = 2; i <= k; i++) {
        node* newP = malloc(sizeof(node));
        if (newP == NULL)
            ERR("malloc");

        newP->num = i;
        newP->next = NULL;

        p->next = newP;
        p = p->next;
    }

    return head;
}

void delete(int i, node** head) {
    if (*head == NULL)
        return;

    node* p = *head;
    node* prev = NULL;

    for (int j = 0; j < i; j++) {
        prev = p;
        p = p->next;
    }

    // if i = 0, delete head
    if (prev == NULL) {
        node* next = (*head)->next;
        free(p);
        *head = next;
        return;
    }
    // else
    prev->next = p->next;
    free(p);
}

void print_list(node* head) {
    node* p = head;

    printf("List: ");
    while (p) {
        printf("%d, ", p->num);
        p = p->next;
    }
    printf("\n");
}

void *thread_work(void *arg) {
    args_t *args = (args_t *)arg;
    printf("[%lu] Worker thread.\n", pthread_self());

    // block all signals
    sigset_t mask;
    sigfillset(&mask);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        ERR("pthread_sigmask");

    // create set mask for sigwait
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);

    int signo;
    srand(time(NULL));

    // sigwait loop
    for (;;) {
        if (sigwait(&mask, &signo) != 0)
            ERR("sigwait");

        switch (signo) {
            case SIGINT:
                if (*(args->size) == 0) {
                    printf("[%lu] List is empty!\n", pthread_self());
                    break;
                }

                int i = rand() % *(args->size);
                printf("[%lu] Delete random number [i=%d]!\n", pthread_self(), i);

                // delete element i
                pthread_mutex_lock(args->head_mtx);
                delete(i, args->head);
                *(args->size) -= 1;
                pthread_mutex_unlock(args->head_mtx);

                break;
            case SIGQUIT:
                // set flag and exit
                pthread_mutex_lock(args->stop_mtx);
                *(args->stop_flag) = 1;
                pthread_mutex_unlock(args->stop_mtx);
                return NULL;
            default:
                break;
        }
    }
}