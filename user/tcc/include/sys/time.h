#ifndef __UCORE_TCC_SYS_TIME_H__
#define __UCORE_TCC_SYS_TIME_H__

#include <time.h>

struct timeval {
    long tv_sec;
    long tv_usec;
};

int gettimeofday(struct timeval *, void *);

#endif
