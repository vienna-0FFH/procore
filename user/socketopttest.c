#include <error.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

int
main(void) {
    int fd;
    int value;
    int type;
    size_t length;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(fd >= 0);
    length = sizeof(type);
    assert(getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) == 0);
    assert(type == SOCK_DGRAM && length == sizeof(type));
    value = 1;
    assert(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == 0);
    value = 0;
    length = sizeof(value);
    assert(getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, &length) == 0 &&
           value == 1);
    value = 8192;
    assert(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) == 0);
    value = 0;
    length = sizeof(value);
    assert(getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, &length) == 0 &&
           value == 8192);
    value = 32;
    assert(setsockopt(fd, IPPROTO_IP, IP_TTL, &value, sizeof(value)) == 0);
    value = 0;
    length = sizeof(value);
    assert(getsockopt(fd, IPPROTO_IP, IP_TTL, &value, &length) == 0 &&
           value == 32);
    assert(setsockopt(fd, IPPROTO_IP, IP_TTL, &value, 1) == -E_INVAL);
    assert(getsockopt(fd, SOL_SOCKET, SO_TYPE, &value, NULL) == -E_INVAL);
    assert(setsockopt(fd, SOL_SOCKET, 999, &value, sizeof(value)) == -E_UNIMP);
    assert(close(fd) == 0);

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    value = 1;
    assert(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) == 0);
    value = 512;
    assert(setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &value, sizeof(value)) == 0);
    length = sizeof(value);
    value = 0;
    assert(getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &value, &length) == 0 &&
           value == 512);
    assert(close(fd) == 0);
    cprintf("socket option get/set test pass.\n");
    return 0;
}
