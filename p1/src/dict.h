#ifndef DICT_H
#define DICT_H

#include "utils.h"

typedef struct Node
{
    char* key;          // src_path&target_path
    pid_t pid;          // pid of the copier process
    struct Node* next;  // ptr to next node in dict list
} Node;

typedef struct Dict
{
    Node* head;
    int size;
    char divider;
} Dict;

Dict* create_dict();

void free_dict(Dict* dict);

char* get_key(Dict* dict, char const* src, char const* target);

void insert(Dict* dict, char* src, char* target, pid_t pid);

pid_t search(Dict* dict, char* src, char* target);

pid_t delete_key(Dict* dict, char* src, char* target);

void delete_pid(Dict* dict, pid_t pid);

void print_dict(Dict* dict);

#endif
