#include "signal_handler.h"

volatile sig_atomic_t last_signal = 0;

// setting handler with sigaction
void set_handler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;

    int res = sigaction(sigNo, &act, NULL);

    if (res == -1 && errno == EINVAL)
        return;

    if (res == -1)
        ERR_KILL("sigaction");
}

// ignore all signals
void set_ign()
{
    // set SIG_IGN for all signals
    for (int i = 1; i < NSIG; i++)
    {
        // you can't ignore SIGKILL & SIGSTOP
        if (i == SIGKILL || i == SIGSTOP)
            continue;
        // set SIG_IGN
        set_handler(SIG_IGN, i);
    }
}

// signal handler for parent and children processes
void sig_handler(int sig) { last_signal = sig; }
