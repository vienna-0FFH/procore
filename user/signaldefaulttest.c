#include <error.h>
#include <signal.h>
#include <stdio.h>
#include <ulib.h>

int
main(void) {
    int child;
    int status;

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        for (;;) {
            yield();
        }
    }
    assert(kill(child, SIGTERM) == 0);
    assert(waitpid(child, &status) == 0);
    assert(status == -E_KILLED);
    cprintf("signal default termination test pass.\n");
    return 0;
}
