#include <poll.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

#define TCP_WINDOW_BYTES 12288

int
main(void) {
    static char payload[TCP_WINDOW_BYTES];
    char received[256];
    struct sockaddr_in server;
    int fd;
    int sent;
    int received_total = 0;
    int i;

    for (i = 0; i < TCP_WINDOW_BYTES; i++) {
        payload[i] = (char)('a' + (i % 23));
    }
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(18081);
    server.sin_addr = NET_GATEWAY_IP;
    assert(connect(fd, &server, sizeof(server)) == 0);
    sent = send(fd, payload, sizeof(payload));
    cprintf("tcpwindow send returned %d\n", sent);
    assert(sent == sizeof(payload));

    while (received_total < TCP_WINDOW_BYTES) {
        struct pollfd waitfd;
        int ret;
        waitfd.fd = fd;
        waitfd.events = POLLIN;
        waitfd.revents = 0;
        assert(poll(&waitfd, 1, 5000) > 0);
        ret = recv(fd, received, sizeof(received));
        assert(ret > 0);
        assert(received_total + ret <= TCP_WINDOW_BYTES);
        for (i = 0; i < ret; i++) {
            assert(received[i] == payload[received_total + i]);
        }
        received_total += ret;
    }
    close(fd);
    cprintf("TCP sliding window echo test pass (%d bytes).\n",
            received_total);
    return 0;
}
