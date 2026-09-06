#include <socket.h>
#include <syscall.h>

int
socket(int domain, int type, int protocol) {
    return sys_socket(domain, type, protocol);
}

int
bind(int fd, const struct sockaddr_in *address, size_t length) {
    return sys_bind(fd, address, length);
}

int
sendto(int fd, const void *data, size_t length,
       const struct sockaddr_in *destination, size_t dest_length) {
    return sys_sendto(fd, data, length, destination, dest_length);
}

int
recvfrom(int fd, void *data, size_t length,
         struct sockaddr_in *source, size_t source_length) {
    return sys_recvfrom(fd, data, length, source, source_length);
}

int
netstat(struct net_stats *stats) {
    return sys_netstat(stats);
}
