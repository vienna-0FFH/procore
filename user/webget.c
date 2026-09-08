#include <netdb.h>
#include <poll.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

int
main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "example.com";
    const char *path = argc > 2 ? argv[2] : "/";
    char request[256];
    char buffer[512];
    struct sockaddr_in server;
    struct pollfd waitfd;
    uint32_t address;
    int fd;
    int ret;

    ret = dns_resolve_ipv4(host, &address);
    if (ret != 0) {
        cprintf("webget: DNS %s failed: %e\n", host, ret);
        return ret;
    }
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return fd;
    }
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    server.sin_addr = address;
    ret = connect(fd, &server, sizeof(server));
    if (ret != 0) {
        cprintf("webget: connect %s failed: %e\n", host, ret);
        close(fd);
        return ret;
    }
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
             path, host);
    if (send(fd, request, strlen(request)) != (int)strlen(request)) {
        close(fd);
        return -1;
    }
    waitfd.fd = fd;
    waitfd.events = POLLIN;
    while ((ret = poll(&waitfd, 1, 5000)) >= 0 &&
           (waitfd.revents & (POLLIN | POLLHUP)) != 0) {
        ret = recv(fd, buffer, sizeof(buffer));
        if (ret <= 0) {
            break;
        }
        write(1, buffer, ret);
    }
    close(fd);
    return ret < 0 ? ret : 0;
}
