#ifndef __UCORE_TCC_TIME_H__
#define __UCORE_TCC_TIME_H__

typedef long time_t;

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

#endif
