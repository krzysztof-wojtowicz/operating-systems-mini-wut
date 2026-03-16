#ifndef WATCHERS_H
#define WATCHERS_H

#include "utils.h"

typedef struct Watch
{
    int wd;              // watcher descriptor
    char* path;          // path to watched dir
    struct Watch* next;  // next watch in list
} Watch;

typedef struct Watchers
{
    Watch* head;  // head of list of watches
    int size;     // size of list
    int fd;       // inotify descriptor
} Watchers;

Watchers* watchers_init();

void free_watchers(Watchers* w);

int add_watch(Watchers* w, char* path);

void delete_watch(Watchers* w, int wd);

Watch* search_watch(Watchers* w, int wd);

void print_watchers(Watchers* w, FILE* logs, char* src, char* target);

void add_watch_recursive(Watchers* w, char* path);

void update_watch_paths(Watchers* w, const char* old_path, const char* new_path);

#endif
