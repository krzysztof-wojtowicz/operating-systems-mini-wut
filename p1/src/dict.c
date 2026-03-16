#include "dict.h"

// create key from src and target paths
char* get_key(Dict* dict, char const* src, char const* target)
{
    int const size = strlen(src) + strlen(target) + 2;
    char divider[2];
    divider[0] = dict->divider;
    divider[1] = '\0';
    char* key = malloc(size * sizeof(char));
    if (key == NULL)
        ERR_KILL("malloc");

    strcpy(key, src);
    strcat(key, divider);
    strcat(key, target);

    return key;
}

// create new empty dictionary with default divider -> '&'
Dict* create_dict()
{
    Dict* new_dict = malloc(sizeof(Dict));
    if (new_dict == NULL)
        ERR_KILL("malloc");

    new_dict->head = NULL;
    new_dict->size = 0;
    new_dict->divider = '&';

    return new_dict;
}

// search for element with given key, return pid if found, else return 0
pid_t search(Dict* dict, char* src, char* target)
{
    Node* p = dict->head;
    char* key = get_key(dict, src, target);

    while (p != NULL)
    {
        if (strcmp(p->key, key) == 0)
        {
            free(key);
            return p->pid;
        }
        p = p->next;
    }

    free(key);
    return 0;
}

// inserts new element at the beginning
void insert(Dict* dict, char* src, char* target, pid_t pid)
{
    char* key = get_key(dict, src, target);

    Node* new_node = malloc(sizeof(Node));
    if (new_node == NULL)
        ERR_KILL("malloc");

    new_node->key = key;
    new_node->pid = pid;
    new_node->next = dict->head;
    // insert at beginning
    dict->head = new_node;
    dict->size++;
}

// deletes element and returns its pid, 0 if element doesn't exist
pid_t delete_key(Dict* dict, char* src, char* target)
{
    if (dict->head == NULL)
        return 0;

    char* key = get_key(dict, src, target);
    Node* p = dict->head;
    Node* prev = NULL;
    pid_t pid = 0;

    if (strcmp(p->key, key) == 0)
    {
        free(key);
        dict->head = p->next;
        pid = p->pid;
        free(p->key);
        free(p);
        dict->size--;
        return pid;
    }

    prev = p;
    p = p->next;

    while (p != NULL)
    {
        if (strcmp(p->key, key) == 0)
        {
            free(key);
            prev->next = p->next;
            pid = p->pid;
            free(p->key);
            free(p);
            dict->size--;
            return pid;
        }
        prev = p;
        p = p->next;
    }

    free(key);
    return 0;
}

// free dict
void free_dict(Dict* dict)
{
    Node* p = dict->head;
    Node* next = NULL;

    while (p != NULL)
    {
        next = p->next;
        free(p->key);
        free(p);
        p = next;
    }

    free(dict);
}

// prints dict
void print_dict(Dict* dict)
{
    Node* p = dict->head;

    while (p != NULL)
    {
        char* src = malloc(sizeof(char) * (strlen(p->key) + 1));
        if (src == NULL)
            ERR_KILL("malloc");

        strcpy(src, p->key);

        char* target = strchr(src, dict->divider);
        if (target == NULL)
        {
            ERR("strchr");
            continue;
        }
        target[0] = '\0';
        target++;

        fprintf(stdout, "  [%d] %s -> %s \n", p->pid, src, target);
        p = p->next;
        free(src);
    }
}

// delete dict element based on pid
void delete_pid(Dict* dict, pid_t pid)
{
    if (dict->head == NULL)
        return;

    Node* p = dict->head;
    Node* prev = NULL;

    if (p->pid == pid)
    {
        dict->head = p->next;
        free(p->key);
        free(p);
        dict->size--;
        return;
    }

    prev = p;
    p = p->next;

    while (p != NULL)
    {
        if (p->pid == pid)
        {
            prev->next = p->next;
            free(p->key);
            free(p);
            dict->size--;
            return;
        }
        prev = p;
        p = p->next;
    }
}
