#ifndef __USER_LIBS_SOCKET_H__
#define __USER_LIBS_SOCKET_H__

#include <unistd.h>

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr_in *address, size_t length);
int sendto(int fd, const void *data, size_t length,
           const struct sockaddr_in *destination, size_t dest_length);
int recvfrom(int fd, void *data, size_t length,
             struct sockaddr_in *source, size_t source_length);
int netstat(struct net_stats *stats);

#endif /* !__USER_LIBS_SOCKET_H__ */
