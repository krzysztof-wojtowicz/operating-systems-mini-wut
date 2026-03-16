#ifndef UTILS_H
#define UTILS_H

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (fprintf(stderr, "Error\n%s:%d\n", __FILE__, __LINE__), perror(source))

#define ERR_KILL(source)                                                                                           \
    (fprintf(stderr, "Critical error, exiting...\n%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), \
     exit(EXIT_FAILURE))

#define EVENT_BUF_LEN (64 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_ARGS 20

void usage();

int check_dir(char* path);

char* join_paths(char* path, char* filename);

void rm_dir_recursive(char* path);

#endif
