#include <error.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static int
timespec_compare(const struct timespec *left, const struct timespec *right) {
    if (left->tv_sec != right->tv_sec) {
        return left->tv_sec < right->tv_sec ? -1 : 1;
    }
    if (left->tv_nsec != right->tv_nsec) {
        return left->tv_nsec < right->tv_nsec ? -1 : 1;
    }
    return 0;
}

int
main(void) {
    struct timespec before;
    struct timespec after;
    struct timespec resolution;
    struct timespec request;
    struct timespec remaining;
    struct timeval wall;
    struct timezone zone;
    int32_t stored;
    volatile uint32_t spin;

    assert(clock_getres(CLOCK_MONOTONIC, &resolution) == 0);
    assert(resolution.tv_sec == 0 && resolution.tv_nsec > 0 &&
           resolution.tv_nsec <= 1000000000);
    assert(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    assert(timespec_compare(&after, &before) >= 0);

    request.tv_sec = 0;
    request.tv_nsec = 20000000;
    assert(nanosleep(&request, &remaining) == 0);
    assert(remaining.tv_sec == 0 && remaining.tv_nsec == 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    assert(timespec_compare(&after, &before) > 0);

    request.tv_sec = -1;
    request.tv_nsec = 0;
    assert(nanosleep(&request, NULL) == -E_INVAL);
    request.tv_sec = 0;
    request.tv_nsec = 1000000000;
    assert(nanosleep(&request, NULL) == -E_INVAL);

    assert(clock_gettime(CLOCK_REALTIME, &before) == 0);
    assert(gettimeofday(&wall, &zone) == 0);
    assert(wall.tv_sec == before.tv_sec);
    assert(zone.tz_minuteswest == 0 && zone.tz_dsttime == 0);
    assert(gettimeofday(NULL, NULL) == 0);
    assert(time(&stored) == stored && stored >= 0);

    assert(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &before) == 0);
    for (spin = 0; spin < 200000U; spin++) {
        if ((spin & 255U) == 0) yield();
    }
    assert(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &after) == 0);
    assert(timespec_compare(&after, &before) >= 0);

    cprintf("clock, realtime, nanosleep and cpu time test pass.\n");
    return 0;
}
