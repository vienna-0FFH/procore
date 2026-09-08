#ifndef __USER_LIBS_FCNTL_H__
#define __USER_LIBS_FCNTL_H__

#include <unistd.h>

int fcntl(int fd, int command, uint32_t argument);

#endif /* !__USER_LIBS_FCNTL_H__ */
