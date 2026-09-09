#include <error.h>
#include <file.h>
#include <fcntl.h>
#include <poll.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

int
main(void) {
    struct sockaddr_in address;
    struct pollfd waitfd;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(18082);
    address.sin_addr = INADDR_ANY;
    assert(bind(fd, &address, sizeof(address)) == 0);
    assert(listen(fd, 4) == 0);
    assert(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
    assert(accept(fd, NULL, 0) == -E_BUSY);

    waitfd.fd = fd;
    waitfd.events = POLLIN;
    waitfd.revents = 0;
    assert(poll(&waitfd, 1, 0) == 0);
    close(fd);
    cprintf("TCP listen API test pass.\n");
    return 0;
}
