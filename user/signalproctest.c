#include <error.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static volatile int user_signal_seen;

static void
signal_handler(int signo) {
    if (signo == SIGUSR1) {
        user_signal_seen = 1;
    }
}

int
main(void) {
    struct sigaction action;
    int child;
    int status;

    memset(&action, 0, sizeof(action));
    action.sa_handler = (uintptr_t)signal_handler;
    assert(sigaction(SIGUSR1, &action, NULL) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(kill(getppid(), SIGUSR1) == 0);
        exit(0);
    }

    /* The handler interrupts the blocked wait once; the child remains a
     * zombie until the second wait reaps it. */
    assert(waitpid(child, &status) == -E_INTR);
    assert(user_signal_seen == 1);
    assert(waitpid(child, &status) == 0 && status == 0);
    cprintf("signal parent delivery and EINTR test pass.\n");
    return 0;
}
