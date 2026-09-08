#ifndef __USER_LIBS_POLL_H__
#define __USER_LIBS_POLL_H__

#include <unistd.h>

int poll(struct pollfd *fds, size_t count, int timeout_ms);

#endif /* !__USER_LIBS_POLL_H__ */
