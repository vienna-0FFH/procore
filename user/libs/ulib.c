#include <defs.h>
#include <syscall.h>
#include <stdio.h>
#include <ulib.h>
#include <stat.h>
#include <string.h>
#include <lock.h>
#include <error.h>
#include <unistd.h>

static lock_t fork_lock = INIT_LOCK;

void
lock_fork(void) {
    lock(&fork_lock);
}

void
unlock_fork(void) {
    unlock(&fork_lock);
}

void
exit(int error_code) {
    sys_exit(error_code);
    cprintf("BUG: exit failed.\n");
    while (1);
}

int
fork(void) {
    return sys_fork();
}

static void __noreturn
clone_start(int (*fn)(void *), void *arg) {
    exit(fn(arg));
}

int
clone(int (*fn)(void *), void *child_stack,
      uint32_t clone_flags, void *arg) {
    if (fn == NULL || child_stack == NULL) {
        return -E_INVAL;
    }
    return sys_clone(clone_flags, child_stack,
                     (uintptr_t)clone_start, (uintptr_t)fn,
                     (uintptr_t)arg);
}

int
wait(void) {
    return sys_wait(0, NULL);
}

int
waitpid(int pid, int *store) {
    return sys_wait(pid, store);
}

int
wait4(int pid, int *store, uint32_t options) {
    return sys_wait4(pid, store, options);
}

int
waitid(int idtype, int id, siginfo_t *info, uint32_t options) {
    return sys_waitid(idtype, id, info, options);
}

void
yield(void) {
    sys_yield();
}

int
getpid(void) {
    return sys_getpid();
}

int
getppid(void) {
    return sys_getppid();
}

int
gettid(void) {
    return sys_gettid();
}

int
getcpu(void) {
    return sys_getcpu();
}

int uname(struct utsname *name) { return sys_uname(name); }
int sysinfo(struct sysinfo *info) { return sys_sysinfo(info); }
int getuid(void) { return sys_getuid(); }
int geteuid(void) { return sys_geteuid(); }
int getgid(void) { return sys_getgid(); }
int getegid(void) { return sys_getegid(); }
int getresuid(uint32_t *real, uint32_t *effective, uint32_t *saved) {
    return sys_getresuid(real, effective, saved);
}
int getresgid(uint32_t *real, uint32_t *effective, uint32_t *saved) {
    return sys_getresgid(real, effective, saved);
}

int
setaffinity(int pid, uint32_t mask) {
    return sys_setaffinity(pid, mask);
}

int
getaffinity(int pid, uint32_t *mask_store) {
    return sys_getaffinity(pid, mask_store);
}

int
getcpustat(int cpu, struct cpu_stat *stat) {
    return sys_getcpustat(cpu, stat);
}

void *
mmap(void *addr, size_t len, uint32_t prot, uint32_t flags) {
    int ret = sys_mmap(addr, len, prot, flags);
    return (ret < 0) ? MAP_FAILED : (void *)(uintptr_t)ret;
}

int
munmap(void *addr, size_t len) {
    return sys_munmap(addr, len);
}

int
mprotect(void *addr, size_t len, uint32_t prot) {
    return sys_mprotect(addr, len, prot);
}

int
getrusage(int who, struct rusage *usage) {
    return sys_getrusage(who, usage);
}

int
madvise(void *addr, size_t len, int advice) {
    return sys_madvise(addr, len, advice);
}

uintptr_t
brk(uintptr_t newbrk) {
    return (uintptr_t)sys_brk(newbrk);
}

//print_pgdir - print the PDT&PT
void
print_pgdir(void) {
    sys_pgdir();
}

void
lab6_set_priority(uint32_t priority)
{
    sys_lab6_set_priority(priority);
}

int
sleep(unsigned int time) {
    return sys_sleep(time);
}

unsigned int
gettime_msec(void) {
    return (unsigned int)sys_gettime();
}

int
clock_gettime(int clock_id, struct timespec *tp) {
    return sys_clock_gettime(clock_id, tp);
}

int
clock_getres(int clock_id, struct timespec *res) {
    return sys_clock_getres(clock_id, res);
}

int
gettimeofday(struct timeval *tv, struct timezone *tz) {
    return sys_gettimeofday(tv, tz);
}

int
nanosleep(const struct timespec *req, struct timespec *rem) {
    return sys_nanosleep(req, rem);
}

int32_t
time(int32_t *store) {
    return (int32_t)sys_time(store);
}

int
__exec(const char *name, const char **argv) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc ++;
    }
    return sys_exec(name, argc, argv);
}
