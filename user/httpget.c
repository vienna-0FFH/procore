#include <error.h>
#include <poll.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

/* Minimal HTTP/1.0 client over the kernel's active TCP path.  The endpoint
 * is QEMU's user-net host gateway; the test harness serves port 18080. */
int
main(void) {
    struct sockaddr_in server;
    struct pollfd waitfd;
    static char request[] =
        "GET / HTTP/1.0\r\nHost: 10.0.2.2\r\nConnection: close\r\n\r\n";
    char buffer[256];
    int fd;
    int ret;
    int total = 0;
    bool header_seen = 0;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(18080);
    server.sin_addr = NET_GATEWAY_IP;
    ret = connect(fd, &server, sizeof(server));
    if (ret != 0) {
        cprintf("httpget: connect failed %d (%e)\n", ret, ret);
        return 1;
    }
    assert(send(fd, request, sizeof(request) - 1) == sizeof(request) - 1);

    waitfd.fd = fd;
    waitfd.events = POLLIN;
    waitfd.revents = 0;
    while (total < (int)sizeof(buffer) * 8) {
        assert(poll(&waitfd, 1, 5000) >= 0);
        if ((waitfd.revents & (POLLIN | POLLHUP)) == 0) {
            continue;
        }
        ret = recv(fd, buffer, sizeof(buffer) - 1);
        if (ret == 0) {
            break;
        }
        assert(ret > 0);
        buffer[ret] = '\0';
        if (total == 0 && strncmp(buffer, "HTTP/1.", 7) == 0) {
            header_seen = 1;
        }
        write(1, buffer, ret);
        total += ret;
    }
    close(fd);
    assert(header_seen && total != 0);
    cprintf("HTTP over TCP client test pass.\n");
    return 0;
}
