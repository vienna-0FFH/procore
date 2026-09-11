#ifndef __UCORE_TCC_TIME_H__
#define __UCORE_TCC_TIME_H__

typedef int time_t;

#define CLOCK_REALTIME             0
#define CLOCK_MONOTONIC            1
#define CLOCK_PROCESS_CPUTIME_ID   2
#define CLOCK_THREAD_CPUTIME_ID    3
#define CLOCK_MONOTONIC_RAW        4
#define CLOCK_REALTIME_COARSE      5
#define CLOCK_MONOTONIC_COARSE     6
#define CLOCK_BOOTTIME             7

#ifndef __UCORE_TIMESPEC_DEFINED
#define __UCORE_TIMESPEC_DEFINED
struct timespec {
    int tv_sec;
    int tv_nsec;
};
#endif

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
};

time_t time(time_t *);
struct tm *localtime(const time_t *);
int clock_gettime(int, struct timespec *);
int clock_getres(int, struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);

#endif
