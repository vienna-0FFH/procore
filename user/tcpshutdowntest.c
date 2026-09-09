#include <error.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

#define TCP_SHUTDOWN_PORT 18081
#define TCP_SHUTDOWN_TEXT "ucore-half-close"

int
main(void) {
    struct sockaddr_in server;
    char received[sizeof(TCP_SHUTDOWN_TEXT) - 1];
    int fd;
    int ret;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(TCP_SHUTDOWN_PORT);
    server.sin_addr = NET_GATEWAY_IP;
    /* Allow the emulated NIC and QEMU user-net ARP proxy to settle before
     * the first active connection attempt. */
    assert(sleep(100) == 0);
    assert(connect(fd, &server, sizeof(server)) == 0);
    assert(send(fd, TCP_SHUTDOWN_TEXT,
                sizeof(TCP_SHUTDOWN_TEXT) - 1) ==
           sizeof(TCP_SHUTDOWN_TEXT) - 1);
    assert(shutdown(fd, SHUT_WR) == 0);

    ret = recv(fd, received, sizeof(received));
    assert(ret == sizeof(received));
    assert(memcmp(received, TCP_SHUTDOWN_TEXT, sizeof(received)) == 0);
    assert(recv(fd, received, sizeof(received)) == 0);
    assert(shutdown(fd, SHUT_RD) == 0);
    close(fd);
    cprintf("TCP half-close and FIN state test pass.\n");
    return 0;
}
