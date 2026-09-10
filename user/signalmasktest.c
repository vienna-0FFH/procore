#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static volatile int signal_seen;

static void
signal_handler(int signo) {
    if (signo == SIGUSR1) {
        signal_seen++;
    }
}

int
main(void) {
    struct sigaction action;
    sigset_t mask;

    memset(&action, 0, sizeof(action));
    action.sa_handler = (uintptr_t)signal_handler;
    assert(sigaction(SIGUSR1, &action, NULL) == 0);
    assert(raise(SIGUSR1) == 0);
    assert(signal_seen == 1);

    mask = (sigset_t)1U << SIGUSR1;
    assert(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
    assert(raise(SIGUSR1) == 0);
    assert(signal_seen == 1);
    assert(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0);
    assert(signal_seen == 2);

    assert(sigaction(SIGKILL, &action, NULL) < 0);
    assert(sigprocmask(SIG_BLOCK,
                       &(sigset_t){ (sigset_t)1U << SIGKILL }, NULL) == 0);
    cprintf("signal handler and mask test pass.\n");
    return 0;
}
