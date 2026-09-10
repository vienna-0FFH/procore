#include <error.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static volatile int child_signal_seen;

static void
child_signal_handler(int signo) {
    if (signo == SIGCHLD) {
        child_signal_seen = 1;
    }
}

int
main(void) {
    struct sigaction action;
    int child;
    int status;

    memset(&action, 0, sizeof(action));
    action.sa_handler = (uintptr_t)child_signal_handler;
    assert(sigaction(SIGCHLD, &action, NULL) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        exit(0);
    }
    assert(waitpid(child, &status) == -E_INTR);
    assert(child_signal_seen == 1);
    assert(waitpid(child, &status) == 0 && status == 0);
    cprintf("SIGCHLD generation and wait interruption test pass.\n");
    return 0;
}
