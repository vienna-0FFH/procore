#ifndef __UCORE_TCC_SYS_TIME_H__
#define __UCORE_TCC_SYS_TIME_H__

#include <time.h>

#ifndef __UCORE_TIMEVAL_DEFINED
#define __UCORE_TIMEVAL_DEFINED
struct timeval {
    int tv_sec;
    int tv_usec;
};
#endif

#ifndef __UCORE_TIMEZONE_DEFINED
#define __UCORE_TIMEZONE_DEFINED
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};
#endif

int gettimeofday(struct timeval *, void *);

#endif
