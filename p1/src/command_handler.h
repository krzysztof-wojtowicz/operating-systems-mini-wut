#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "dict.h"
#include "signal_handler.h"
#include "utils.h"
#include "worker.h"

void handle_add(Dict* dict, char** argv, int argc, FILE* logs);

void handle_end(Dict* dict, char** argv, int argc);

void handle_list(Dict* dict);

void handle_restore(Dict* dict, char** argv, int argc, FILE* logs);

void handle_exit(Dict* dict);

int check_path(char* path);

void restore(Dict* dict, char* src, char* target, FILE* logs);

void restore_better(Dict* dict, char* src, char* target, FILE* logs);

void delete_recursive(char* src, char* target, FILE* logs);

void restore_recursive(char* src, char* target, FILE* logs);

#endif
