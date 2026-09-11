#include <error.h>
#include <file.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static int
select_width(int left, int right) {
    return left > right ? left : right;
}

int
main(void) {
    int fds[2];
    int nfds;
    int ready;
    char value = 's';
    char received = 0;
    fd_set readfds;
    fd_set writefds;
    struct timeval timeout;

    assert(pipe(fds) == 0);
    nfds = select_width(fds[0], fds[1]) + 1;

    FD_ZERO(&readfds);
    FD_SET(fds[0], &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    ready = select(nfds, &readfds, NULL, NULL, &timeout);
    assert(ready == 0 && !FD_ISSET(fds[0], &readfds));

    assert(write(fds[1], &value, sizeof(value)) == 1);
    FD_ZERO(&readfds);
    FD_SET(fds[0], &readfds);
    FD_ZERO(&writefds);
    FD_SET(fds[1], &writefds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    ready = select(nfds, &readfds, &writefds, NULL, &timeout);
    assert(ready == 2 && FD_ISSET(fds[0], &readfds) &&
           FD_ISSET(fds[1], &writefds));
    assert(read(fds[0], &received, sizeof(received)) == 1 && received == value);

    FD_ZERO(&readfds);
    FD_SET(fds[0], &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;
    ready = select(nfds, &readfds, NULL, NULL, &timeout);
    assert(ready == 0 && !FD_ISSET(fds[0], &readfds));

    timeout.tv_sec = -1;
    timeout.tv_usec = 0;
    assert(select(nfds, NULL, NULL, NULL, &timeout) == -E_INVAL);
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000000;
    assert(select(nfds, NULL, NULL, NULL, &timeout) == -E_INVAL);
    assert(select(UCORE_FD_SETSIZE + 1, NULL, NULL, NULL, NULL) == -E_INVAL);
    FD_ZERO(&readfds);
    FD_SET(fds[0], &readfds);
    assert(select(nfds, &readfds, NULL, &readfds, NULL) == -E_UNIMP);
    assert(close(fds[0]) == 0 && close(fds[1]) == 0);
    cprintf("select fd sets and timeout test pass.\n");
    return 0;
}
