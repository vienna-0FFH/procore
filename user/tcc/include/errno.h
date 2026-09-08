#ifndef __UCORE_TCC_ERRNO_H__
#define __UCORE_TCC_ERRNO_H__

#include <defs.h>

extern int errno;

#define EINVAL 22
#define ENOMEM 12
#define ENOENT 2
#define EACCES 13
#define EBADF 9
#define EIO 5
#define EEXIST 17
#define ENOSPC 28
#define ENOTDIR 20
#define EISDIR 21
#define EOVERFLOW 75
#define EILSEQ 84
#define EINTR 4

#endif
