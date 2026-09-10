#include <error.h>
#include <file.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static volatile int continued;

static void
continue_handler(int signo) {
    if (signo == SIGCONT) {
        continued = 1;
    }
}

int
main(void) {
    int fds[2];
    int child;
    int status;
    char marker;
    struct pollfd pfd;
    struct sigaction action;

    assert(pipe(fds) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        memset(&action, 0, sizeof(action));
        action.sa_handler = (uintptr_t)continue_handler;
        assert(sigaction(SIGCONT, &action, NULL) == 0);
        close(fds[0]);
        marker = 'R';
        assert(write(fds[1], &marker, 1) == 1);
        while (!continued) {
            yield();
        }
        marker = 'C';
        assert(write(fds[1], &marker, 1) == 1);
        close(fds[1]);
        exit(0);
    }

    close(fds[1]);
    assert(read(fds[0], &marker, 1) == 1 && marker == 'R');
    assert(kill(child, SIGSTOP) == 0);
    sleep(20);
    assert(kill(child, SIGCONT) == 0);
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    assert(poll(&pfd, 1, 1000) == 1 && (pfd.revents & POLLIN) != 0);
    assert(read(fds[0], &marker, 1) == 1 && marker == 'C');
    assert(waitpid(child, &status) == 0 && status == 0);
    close(fds[0]);
    cprintf("signal stop/continue test pass.\n");
    return 0;
}
