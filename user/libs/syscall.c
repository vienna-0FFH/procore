#include <defs.h>
#include <unistd.h>
#include <syscall.h>
#include <stat.h>
#include <dirent.h>


/*
 * Keep the user ABI explicit about the number of arguments.  The old
 * variadic helper unconditionally consumed five va_arg() values, even for
 * syscall wrappers that supplied none; that is undefined behaviour and can
 * consume unrelated stack/register arguments.  Fixed-arity entry points also
 * make the i386 register ABI visible at each call site.
 */
static inline int
syscall_impl(int num, uint32_t a0, uint32_t a1, uint32_t a2,
             uint32_t a3, uint32_t a4) {
    int ret;
    asm volatile (
        "int %1;"
        : "=a" (ret)
        : "i" (T_SYSCALL),
          "a" (num),
          "d" (a0),
          "c" (a1),
          "b" (a2),
          "D" (a3),
          "S" (a4)
        : "cc", "memory");
    return ret;
}

#define syscall0(num) \
    syscall_impl((num), 0, 0, 0, 0, 0)
#define syscall1(num, a0) \
    syscall_impl((num), (uint32_t)(a0), 0, 0, 0, 0)
#define syscall2(num, a0, a1) \
    syscall_impl((num), (uint32_t)(a0), (uint32_t)(a1), 0, 0, 0)
#define syscall3(num, a0, a1, a2) \
    syscall_impl((num), (uint32_t)(a0), (uint32_t)(a1), (uint32_t)(a2), 0, 0)
#define syscall4(num, a0, a1, a2, a3) \
    syscall_impl((num), (uint32_t)(a0), (uint32_t)(a1), (uint32_t)(a2), \
                 (uint32_t)(a3), 0)
#define syscall5(num, a0, a1, a2, a3, a4) \
    syscall_impl((num), (uint32_t)(a0), (uint32_t)(a1), (uint32_t)(a2), \
                 (uint32_t)(a3), (uint32_t)(a4))

int
sys_exit(int error_code) {
    return syscall1(SYS_exit, error_code);
}

int
sys_fork(void) {
    return syscall0(SYS_fork);
}

int
sys_wait(int pid, int *store) {
    return syscall2(SYS_wait, pid, (uintptr_t)store);
}

int
sys_yield(void) {
    return syscall0(SYS_yield);
}

int
sys_kill(int pid) {
    return syscall1(SYS_kill, pid);
}

int
sys_getpid(void) {
    return syscall0(SYS_getpid);
}

int
sys_putc(int c) {
    return syscall1(SYS_putc, c);
}

int
sys_pgdir(void) {
    return syscall0(SYS_pgdir);
}

void
sys_lab6_set_priority(uint32_t priority)
{
    syscall1(SYS_lab6_set_priority, priority);
}

int
sys_sleep(unsigned int time) {
    return syscall1(SYS_sleep, time);
}

size_t
sys_gettime(void) {
    return syscall0(SYS_gettime);
}

int
sys_exec(const char *name, int argc, const char **argv) {
    return syscall3(SYS_exec, (uintptr_t)name, argc, (uintptr_t)argv);
}

int
sys_open(const char *path, uint32_t open_flags) {
    return syscall2(SYS_open, (uintptr_t)path, open_flags);
}

int
sys_close(int fd) {
    return syscall1(SYS_close, fd);
}

int
sys_read(int fd, void *base, size_t len) {
    return syscall3(SYS_read, fd, (uintptr_t)base, len);
}

int
sys_write(int fd, void *base, size_t len) {
    return syscall3(SYS_write, fd, (uintptr_t)base, len);
}

int
sys_seek(int fd, off_t pos, int whence) {
    return syscall3(SYS_seek, fd, pos, whence);
}

int
sys_fstat(int fd, struct stat *stat) {
    return syscall2(SYS_fstat, fd, (uintptr_t)stat);
}

int
sys_fsync(int fd) {
    return syscall1(SYS_fsync, fd);
}

int
sys_chdir(const char *path) {
    return syscall1(SYS_chdir, (uintptr_t)path);
}

int
sys_getcwd(char *buffer, size_t len) {
    return syscall2(SYS_getcwd, (uintptr_t)buffer, len);
}

int
sys_getdirentry(int fd, struct dirent *dirent) {
    return syscall2(SYS_getdirentry, fd, (uintptr_t)dirent);
}

int
sys_dup(int fd1, int fd2) {
    return syscall2(SYS_dup, fd1, fd2);
}
