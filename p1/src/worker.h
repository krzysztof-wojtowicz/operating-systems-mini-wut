#ifndef WORKER_H
#define WORKER_H

#include "signal_handler.h"
#include "utils.h"
#include "watchers.h"

void start_worker(char* src, char* target, FILE* logs);

void write_log(FILE* logs, char* src, char* target, char* msg, char* arg);

void copy_file(char* file1, char* file2);

void copy_symlink(char* file1, char* file2, char* src, char* target, FILE* logs);

void copy_dir(char* path1, char* path2, char* src_path, char* target_path, FILE* logs);

int path_cmp(char* path1, char* path2);

void read_watch(Watchers* w, char* src, char* target, FILE* logs);

char* src2target_path(char* event_path, char* src_path, char* target);

void copy_permissions(char* file1, char* file2);

#endif
