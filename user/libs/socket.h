#ifndef __USER_LIBS_SOCKET_H__
#define __USER_LIBS_SOCKET_H__

#include <unistd.h>

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int fd[2]);
int bind(int fd, const struct sockaddr_in *address, size_t length);
int connect(int fd, const struct sockaddr_in *address, size_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr_in *address, size_t length);
int shutdown(int fd, int how);
int sendto(int fd, const void *data, size_t length,
           const struct sockaddr_in *destination, size_t dest_length);
int recvfrom(int fd, void *data, size_t length,
             struct sockaddr_in *source, size_t source_length);
int send(int fd, const void *data, size_t length);
int recv(int fd, void *data, size_t length);
int getsockname(int fd, struct sockaddr_in *address, size_t length);
int getpeername(int fd, struct sockaddr_in *address, size_t length);
int getsockopt(int fd, int level, int option, void *value, size_t *length);
int setsockopt(int fd, int level, int option, const void *value, size_t length);
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
int netstat(struct net_stats *stats);

#endif /* !__USER_LIBS_SOCKET_H__ */
