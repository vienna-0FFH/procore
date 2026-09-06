#include <ulib.h>
#include <unistd.h>
#include <socket.h>
#include <file.h>
#include <string.h>
#include <stdio.h>

/* QEMU user-net forwards host UDP port 19100 to guest port 9100.  The test
 * intentionally binds INADDR_ANY so the same binary remains valid when the
 * configured guest address changes. */
int
main(void) {
    struct sockaddr_in address;
    struct sockaddr_in source;
    char buffer[64];
    int fd;
    int ret;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(9100);
    address.sin_addr = INADDR_ANY;
    assert(bind(fd, &address, sizeof(address)) == 0);
    /* Give the host-side test harness time to attach its UDP sender. */
    assert(sleep(300) == 0);
    memset(buffer, 0, sizeof(buffer));
    memset(&source, 0, sizeof(source));
    ret = recvfrom(fd, buffer, sizeof(buffer), &source, sizeof(source));
    assert(ret == 9 && memcmp(buffer, "net-probe", 9) == 0);
    assert(source.sin_family == AF_INET);
    close(fd);
    cprintf("external Ethernet/IPv4/UDP receive test pass.\n");
    return 0;
}
