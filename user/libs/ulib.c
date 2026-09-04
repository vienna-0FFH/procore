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

void
yield(void) {
    sys_yield();
}

int
kill(int pid) {
    return sys_kill(pid);
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

void *
mmap(void *addr, size_t len, uint32_t prot, uint32_t flags) {
    int ret = sys_mmap(addr, len, prot, flags);
    return (ret < 0) ? MAP_FAILED : (void *)(uintptr_t)ret;
}

int
munmap(void *addr, size_t len) {
    return sys_munmap(addr, len);
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
__exec(const char *name, const char **argv) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc ++;
    }
    return sys_exec(name, argc, argv);
}
