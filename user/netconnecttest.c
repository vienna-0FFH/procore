#include <error.h>
#include <file.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

int
main(void) {
    struct sockaddr_in left;
    struct sockaddr_in right;
    struct sockaddr_in name;
    struct sockaddr_in peer;
    char message[] = "connected udp";
    char reply[] = "reply";
    char buffer[32];
    int lfd;
    int rfd;

    memset(&left, 0, sizeof(left));
    left.sin_family = AF_INET;
    left.sin_port = htons(9011);
    left.sin_addr = INADDR_LOOPBACK;
    memset(&right, 0, sizeof(right));
    right.sin_family = AF_INET;
    right.sin_port = htons(9012);
    right.sin_addr = INADDR_LOOPBACK;

    lfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    rfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(lfd >= 0 && rfd >= 0);
    assert(bind(lfd, &left, sizeof(left)) == 0);
    assert(bind(rfd, &right, sizeof(right)) == 0);
    assert(connect(lfd, &right, sizeof(right)) == 0);
    assert(connect(rfd, &left, sizeof(left)) == 0);
    assert(getsockname(lfd, &name, sizeof(name)) == 0);
    assert(name.sin_port == left.sin_port &&
           name.sin_addr == left.sin_addr);
    assert(getpeername(lfd, &peer, sizeof(peer)) == 0);
    assert(peer.sin_port == right.sin_port &&
           peer.sin_addr == right.sin_addr);

    assert(send(lfd, message, sizeof(message)) == sizeof(message));
    memset(buffer, 0, sizeof(buffer));
    assert(recv(rfd, buffer, sizeof(buffer)) == sizeof(message));
    assert(strcmp(buffer, message) == 0);
    assert(send(rfd, reply, sizeof(reply)) == sizeof(reply));
    memset(buffer, 0, sizeof(buffer));
    assert(recv(lfd, buffer, sizeof(buffer)) == sizeof(reply));
    assert(strcmp(buffer, reply) == 0);
    close(lfd);
    close(rfd);
    cprintf("connected UDP socket API test pass.\n");
    return 0;
}
