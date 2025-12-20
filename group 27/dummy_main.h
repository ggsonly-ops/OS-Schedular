#ifndef DUMMY_MAIN_H
#define DUMMY_MAIN_H

#include <signal.h>
#include <unistd.h>

int dummy_main(int argc, char **argv);

int main(int argc, char **argv) {
    /* Wait for SIGCONT from scheduler before starting execution */
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCONT);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    int sig;
    sigwait(&sigset, &sig);

    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    int ret = dummy_main(argc, argv);
    return ret;
}

#define main dummy_main

#endif
