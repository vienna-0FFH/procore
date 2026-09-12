#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <file.h>
#include <socket.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include <error.h>
#include <unistd.h>

int
main(void) {
    int fds[2];
    int bad[2];
    int duplicate;
    struct pollfd pfd;
    char buffer[16];
    static const char first[] = "socketpair";
    static const char second[] = "reverse";

    assert(socketpair(AF_INET, SOCK_STREAM, 0, bad) == -E_INVAL);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(fds[0] != fds[1]);
    duplicate = dup(fds[0]);
    assert(duplicate >= 0 && duplicate != fds[0] && duplicate != fds[1]);
    assert(close(fds[0]) == 0);
    fds[0] = duplicate;

    pfd.fd = fds[1];
    pfd.events = POLLIN;
    pfd.revents = 0;
    assert(poll(&pfd, 1, 0) == 0 && pfd.revents == 0);

    assert(write(fds[0], (void *)first, sizeof(first) - 1) ==
           (int)(sizeof(first) - 1));
    assert(poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN) != 0);
    memset(buffer, 0, sizeof(buffer));
    assert(read(fds[1], buffer, sizeof(first) - 1) ==
           (int)(sizeof(first) - 1));
    assert(strcmp(buffer, first) == 0);

    assert(write(fds[1], (void *)second, sizeof(second) - 1) ==
           (int)(sizeof(second) - 1));
    memset(buffer, 0, sizeof(buffer));
    assert(read(fds[0], buffer, sizeof(second) - 1) ==
           (int)(sizeof(second) - 1));
    assert(strcmp(buffer, second) == 0);

    assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
    assert(read(fds[0], buffer, sizeof(buffer)) == -E_BUSY);
    assert(fcntl(fds[0], F_SETFL, 0) == 0);

    /* Closing one endpoint wakes the other side and reports EOF/HUP. */
    assert(close(fds[1]) == 0);
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    assert(poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLHUP) != 0);
    assert(read(fds[0], buffer, sizeof(buffer)) == 0);
    assert(close(fds[0]) == 0);

    cprintf("socketpair test pass.\n");
    return 0;
}
