#ifndef __USER_LIBS_POLL_H__
#define __USER_LIBS_POLL_H__

#include <unistd.h>

int poll(struct pollfd *fds, size_t count, int timeout_ms);
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);

#endif /* !__USER_LIBS_POLL_H__ */
