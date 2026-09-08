#include <error.h>
#include <fcntl.h>
#include <file.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

int
main(void) {
    struct pollfd descriptor;
    int fds[2];
    int duplicate;
    char value = 'p';
    char buffer;
    int flags;

    assert(pipe(fds) == 0);
    assert(fcntl(fds[0], F_GETFL, 0) == O_RDONLY);
    assert(fcntl(fds[1], F_GETFL, 0) == O_WRONLY);
    assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
    assert(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);
    flags = fcntl(fds[0], F_GETFL, 0);
    assert((flags & O_NONBLOCK) != 0 && (flags & O_ACCMODE) == O_RDONLY);
    assert(read(fds[0], &buffer, sizeof(buffer)) == -E_BUSY);

    descriptor.fd = fds[0];
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    assert(poll(&descriptor, 1, 0) == 0 && descriptor.revents == 0);
    assert(write(fds[1], &value, sizeof(value)) == 1);
    assert(poll(&descriptor, 1, 100) == 1);
    assert((descriptor.revents & POLLIN) != 0);
    assert(read(fds[0], &buffer, sizeof(buffer)) == 1 && buffer == value);
    descriptor.revents = 0;
    assert(poll(&descriptor, 1, 10) == 0 && descriptor.revents == 0);

    duplicate = fcntl(fds[0], F_DUPFD, 5);
    assert(duplicate >= 5);
    assert(close(duplicate) == 0);
    assert(close(fds[1]) == 0);
    descriptor.revents = 0;
    assert(poll(&descriptor, 1, 0) == 1 &&
           (descriptor.revents & POLLHUP) != 0);
    assert(close(fds[0]) == 0);
    cprintf("fcntl nonblocking and poll readiness test pass.\n");
    return 0;
}
