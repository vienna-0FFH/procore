#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>

static void
spin_work(void) {
    volatile uint32_t value = 0;
    uint32_t i;
    for (i = 0; i < 200000U; i++) {
        value = value * 33U + i;
    }
    (void)value;
}

static int
time_not_before(const struct timeval *left, const struct timeval *right) {
    return left->tv_sec > right->tv_sec ||
           (left->tv_sec == right->tv_sec &&
            left->tv_usec >= right->tv_usec);
}

int
main(void) {
    struct rusage before, after, child_before, child_after;
    int child, status;

    assert(getrusage(RUSAGE_SELF, &before) == 0);
    assert(getrusage(RUSAGE_THREAD, &after) == 0);
    assert(getrusage(99, &after) == -E_INVAL);
    assert(getrusage(RUSAGE_SELF,
                     (struct rusage *)(uintptr_t)-1) == -E_INVAL);

    spin_work();
    assert(sleep(2) == 0);
    assert(getrusage(RUSAGE_SELF, &after) == 0);
    assert(time_not_before(&after.ru_utime, &before.ru_utime));
    assert(after.ru_stime.tv_sec >= 0 && after.ru_stime.tv_usec >= 0);

    assert(getrusage(RUSAGE_CHILDREN, &child_before) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        spin_work();
        assert(sleep(1) == 0);
        exit(0);
    }
    assert(waitpid(child, &status) == 0 && status == 0);
    assert(getrusage(RUSAGE_CHILDREN, &child_after) == 0);
    assert(time_not_before(&child_after.ru_utime, &child_before.ru_utime));
    assert(child_after.ru_nvcsw >= child_before.ru_nvcsw);

    cprintf("getrusage test pass. self=%d.%06d children=%d.%06d\n",
            after.ru_utime.tv_sec, after.ru_utime.tv_usec,
            child_after.ru_utime.tv_sec, child_after.ru_utime.tv_usec);
    return 0;
}
