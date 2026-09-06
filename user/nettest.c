#include <ulib.h>
#include <unistd.h>
#include <socket.h>
#include <file.h>
#include <string.h>
#include <stdio.h>

int
main(void) {
    struct sockaddr_in receiver;
    struct sockaddr_in source;
    char message[] = "ucore udp loopback";
    char buffer[64];
    int rx, rx_dup, tx;
    int ret;
    struct net_stats stats;

    rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(rx >= 0 && tx >= 0);
    memset(&receiver, 0, sizeof(receiver));
    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(9000);
    receiver.sin_addr = INADDR_LOOPBACK;
    assert(bind(rx, &receiver, sizeof(receiver)) == 0);
    rx_dup = dup(rx);
    assert(rx_dup >= 0);
    assert(sendto(tx, message, sizeof(message), &receiver,
                  sizeof(receiver)) == sizeof(message));
    memset(buffer, 0, sizeof(buffer));
    memset(&source, 0, sizeof(source));
    ret = recvfrom(rx_dup, buffer, sizeof(buffer), &source, sizeof(source));
    assert(ret == sizeof(message));
    assert(strcmp(buffer, message) == 0);
    assert(source.sin_family == AF_INET);
    close(rx_dup);
    close(rx);
    close(tx);
    assert(netstat(&stats) == 0 && stats.tx_packets >= 1 &&
           stats.rx_packets >= 1);
    cprintf("udp loopback/socket fd test pass.\n");
    return 0;
}
