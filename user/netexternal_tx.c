#include <ulib.h>
#include <unistd.h>
#include <socket.h>
#include <file.h>
#include <string.h>
#include <stdio.h>

/* The guest sends a datagram through QEMU user-net.  The host harness binds
 * UDP 19101 and supplies the guest's configured gateway as the destination. */
int
main(void) {
    struct sockaddr_in destination;
    char message[] = "tx-probe";
    int fd;
    int ret;
    struct net_stats stats;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(fd >= 0);
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(19101);
    destination.sin_addr = NET_GATEWAY_IP;
    ret = sendto(fd, message, sizeof(message) - 1, &destination,
                 sizeof(destination));
    if (ret != sizeof(message) - 1) {
        cprintf("external TX sendto failed: %d\n", ret);
        return 1;
    }
    assert(netstat(&stats) == 0 && stats.hw_tx_errors == 0);
    cprintf("external Ethernet/IPv4/UDP transmit test pass.\n");
    return 0;
}
