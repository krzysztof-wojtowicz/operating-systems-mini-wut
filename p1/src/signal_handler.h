#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include "utils.h"

extern volatile sig_atomic_t last_signal;

void set_ign();

void set_handler(void (*f)(int), int sigNo);

void sig_handler(int sig);

#endif
