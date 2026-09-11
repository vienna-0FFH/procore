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
connect(int fd, const struct sockaddr_in *address, size_t length) {
    return sys_connect(fd, address, length);
}

int
listen(int fd, int backlog) {
    return sys_listen(fd, backlog);
}

int
accept(int fd, struct sockaddr_in *address, size_t length) {
    return sys_accept(fd, address, length);
}

int
shutdown(int fd, int how) {
    return sys_shutdown(fd, how);
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
send(int fd, const void *data, size_t length) {
    return sys_send(fd, data, length);
}

int
recv(int fd, void *data, size_t length) {
    return sys_recv(fd, data, length);
}

int
getsockname(int fd, struct sockaddr_in *address, size_t length) {
    return sys_getsockname(fd, address, length);
}

int
getpeername(int fd, struct sockaddr_in *address, size_t length) {
    return sys_getpeername(fd, address, length);
}

int getsockopt(int fd, int level, int option, void *value, size_t *length) {
    return sys_getsockopt(fd, level, option, value, length);
}

int setsockopt(int fd, int level, int option, const void *value, size_t length) {
    return sys_setsockopt(fd, level, option, value, length);
}

int
poll(struct pollfd *fds, size_t count, int timeout_ms) {
    return sys_poll(fds, count, timeout_ms);
}

int
netstat(struct net_stats *stats) {
    return sys_netstat(stats);
}
