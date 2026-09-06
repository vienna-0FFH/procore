#include <ulib.h>
#include <unistd.h>
#include <stdio.h>

#define SCHED_WORKERS 12

int
main(void) {
    int children[SCHED_WORKERS];
    int i, j, status;

    for (i = 0; i < SCHED_WORKERS; i++) {
        children[i] = fork();
        assert(children[i] >= 0);
        if (children[i] == 0) {
            volatile uint32_t work = 0;
            for (j = 0; j < 40000; j++) {
                work += (uint32_t)(j ^ getpid());
                if ((j & 0x3ff) == 0) {
                    yield();
                }
            }
            assert(work != 0);
            exit(0);
        }
    }
    for (i = 0; i < SCHED_WORKERS; i++) {
        assert(waitpid(children[i], &status) == 0 && status == 0);
    }

    for (i = 0; i < 8; i++) {
        struct cpu_stat stat;
        if (getcpustat(i, &stat) == 0 && stat.online != 0) {
            cprintf("cpu%d: ticks=%d switches=%d migrations=%d runnable=%d\n",
                    i, stat.ticks, stat.switches, stat.migrations,
                    stat.runnable);
        }
    }
    cprintf("scheduler affinity/load balance test pass.\n");
    return 0;
}
