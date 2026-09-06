#include <ulib.h>
#include <unistd.h>
#include <stdio.h>

int
main(void) {
    uint32_t original;
    struct cpu_stat stat;
    int cpu;

    assert(getaffinity(0, &original) == 0);
    assert(original != 0);
    assert(setaffinity(0, 1U) == 0);
    cpu = getcpu();
    assert(cpu == 0);
    assert(getcpustat(cpu, &stat) == 0);
    assert(stat.online != 0 && stat.cpu_id == (uint32_t)cpu);
    assert(stat.ticks >= stat.idle_ticks);
    assert(setaffinity(0, original) == 0);
    cprintf("affinity/cpu statistics test pass. cpu=%d ticks=%d switches=%d migrations=%d\n",
            cpu, stat.ticks, stat.switches, stat.migrations);
    return 0;
}
