#ifndef PARSER_H
#define PARSER_H

#include "utils.h"

void trim_spaces(char* buf);

int parse_command(char* cmd, char** argv);

void free_argv(char** argv, int argc);

#endif
