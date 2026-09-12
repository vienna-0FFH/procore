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
sys_clone(uint32_t clone_flags, void *child_stack,
          uintptr_t entry, uintptr_t fn, uintptr_t arg) {
    return syscall5(SYS_clone, clone_flags, (uintptr_t)child_stack,
                    entry, fn, arg);
}

int
sys_wait(int pid, int *store) {
    return syscall2(SYS_wait, pid, (uintptr_t)store);
}

int
sys_wait4(int pid, int *store, uint32_t options) {
    return syscall3(SYS_wait4, pid, (uintptr_t)store, options);
}

int
sys_waitid(int idtype, int id, siginfo_t *info, uint32_t options) {
    return syscall4(SYS_waitid, idtype, id, (uintptr_t)info, options);
}

int
sys_yield(void) {
    return syscall0(SYS_yield);
}

int
sys_kill(int pid, int signo) {
    return syscall2(SYS_kill, pid, signo);
}

int
sys_raise(int signo) {
    return syscall1(SYS_raise, signo);
}

int
sys_sigaction(int signo, const struct sigaction *action,
              struct sigaction *old_action) {
    return syscall3(SYS_sigaction, signo, (uintptr_t)action,
                    (uintptr_t)old_action);
}

int
sys_sigprocmask(int how, const sigset_t *set, sigset_t *old_set) {
    return syscall3(SYS_sigprocmask, how, (uintptr_t)set,
                    (uintptr_t)old_set);
}

int
sys_sigreturn(void) {
    return syscall0(SYS_sigreturn);
}

int
sys_getpid(void) {
    return syscall0(SYS_getpid);
}

int
sys_getppid(void) {
    return syscall0(SYS_getppid);
}

int
sys_gettid(void) {
    return syscall0(SYS_gettid);
}

int
sys_getcpu(void) {
    return syscall0(SYS_getcpu);
}

int sys_uname(struct utsname *name) {
    return syscall1(SYS_uname, (uintptr_t)name);
}
int sys_sysinfo(struct sysinfo *info) {
    return syscall1(SYS_sysinfo, (uintptr_t)info);
}
int sys_getuid(void) { return syscall0(SYS_getuid); }
int sys_geteuid(void) { return syscall0(SYS_geteuid); }
int sys_getgid(void) { return syscall0(SYS_getgid); }
int sys_getegid(void) { return syscall0(SYS_getegid); }
int sys_getresuid(uint32_t *real, uint32_t *effective, uint32_t *saved) {
    return syscall3(SYS_getresuid, (uintptr_t)real, (uintptr_t)effective,
                    (uintptr_t)saved);
}
int sys_getresgid(uint32_t *real, uint32_t *effective, uint32_t *saved) {
    return syscall3(SYS_getresgid, (uintptr_t)real, (uintptr_t)effective,
                    (uintptr_t)saved);
}

int
sys_setaffinity(int pid, uint32_t mask) {
    return syscall2(SYS_setaffinity, pid, mask);
}

int
sys_getaffinity(int pid, uint32_t *mask_store) {
    return syscall2(SYS_getaffinity, pid, (uintptr_t)mask_store);
}

int
sys_getcpustat(int cpu, struct cpu_stat *stat) {
    return syscall2(SYS_getcpustat, cpu, (uintptr_t)stat);
}

int
sys_socket(int domain, int type, int protocol) {
    return syscall3(SYS_socket, domain, type, protocol);
}

int
sys_socketpair(int domain, int type, int protocol, int fd[2]) {
    return syscall4(SYS_socketpair, domain, type, protocol, (uintptr_t)fd);
}

int
sys_symlink(const char *target, const char *link_path) {
    return syscall2(SYS_symlink, (uintptr_t)target, (uintptr_t)link_path);
}

int
sys_readlink(const char *path, char *buffer, size_t len) {
    return syscall3(SYS_readlink, (uintptr_t)path, (uintptr_t)buffer, len);
}

int
sys_bind(int fd, const struct sockaddr_in *address, size_t length) {
    return syscall3(SYS_bind, fd, (uintptr_t)address, length);
}

int
sys_sendto(int fd, const void *data, size_t length,
           const struct sockaddr_in *destination, size_t dest_length) {
    return syscall5(SYS_sendto, fd, (uintptr_t)data, length,
                    (uintptr_t)destination, dest_length);
}

int
sys_recvfrom(int fd, void *data, size_t length,
             struct sockaddr_in *source, size_t source_length) {
    return syscall5(SYS_recvfrom, fd, (uintptr_t)data, length,
                    (uintptr_t)source, source_length);
}

int
sys_netstat(struct net_stats *stats) {
    return syscall1(SYS_netstat, (uintptr_t)stats);
}

int
sys_connect(int fd, const struct sockaddr_in *address, size_t length) {
    return syscall3(SYS_connect, fd, (uintptr_t)address, length);
}

int
sys_listen(int fd, int backlog) {
    return syscall2(SYS_listen, fd, backlog);
}

int
sys_accept(int fd, struct sockaddr_in *address, size_t length) {
    return syscall3(SYS_accept, fd, (uintptr_t)address, length);
}

int
sys_shutdown(int fd, int how) {
    return syscall2(SYS_shutdown, fd, how);
}

int
sys_send(int fd, const void *data, size_t length) {
    return syscall3(SYS_send, fd, (uintptr_t)data, length);
}

int
sys_recv(int fd, void *data, size_t length) {
    return syscall3(SYS_recv, fd, (uintptr_t)data, length);
}

int
sys_getsockname(int fd, struct sockaddr_in *address, size_t length) {
    return syscall3(SYS_getsockname, fd, (uintptr_t)address, length);
}

int
sys_getpeername(int fd, struct sockaddr_in *address, size_t length) {
    return syscall3(SYS_getpeername, fd, (uintptr_t)address, length);
}

int sys_getsockopt(int fd, int level, int option, void *value, size_t *length) {
    return syscall5(SYS_getsockopt, fd, level, option, (uintptr_t)value,
                    (uintptr_t)length);
}

int sys_setsockopt(int fd, int level, int option, const void *value, size_t length) {
    return syscall5(SYS_setsockopt, fd, level, option, (uintptr_t)value, length);
}

int sys_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout) {
    return syscall5(SYS_select, nfds, (uintptr_t)readfds, (uintptr_t)writefds,
                    (uintptr_t)exceptfds, (uintptr_t)timeout);
}

int
sys_fcntl(int fd, int command, uint32_t argument) {
    return syscall3(SYS_fcntl, fd, command, argument);
}

int
sys_poll(struct pollfd *fds, size_t count, int timeout_ms) {
    return syscall3(SYS_poll, (uintptr_t)fds, count, timeout_ms);
}

int
sys_mmap(void *addr, size_t len, uint32_t prot, uint32_t flags) {
    return syscall4(SYS_mmap, (uintptr_t)addr, len, prot, flags);
}

int
sys_munmap(void *addr, size_t len) {
    return syscall2(SYS_munmap, (uintptr_t)addr, len);
}

int
sys_mprotect(void *addr, size_t len, uint32_t prot) {
    return syscall3(SYS_mprotect, (uintptr_t)addr, len, prot);
}

int
sys_getrusage(int who, struct rusage *usage) {
    return syscall2(SYS_getrusage, who, (uintptr_t)usage);
}

int
sys_madvise(void *addr, size_t len, int advice) {
    return syscall3(SYS_madvise, (uintptr_t)addr, len, advice);
}

int
sys_futex(uint32_t *address, int operation, uint32_t expected,
          const struct timespec *timeout) {
    return syscall4(SYS_futex, (uintptr_t)address, operation, expected,
                    (uintptr_t)timeout);
}

int
sys_brk(uintptr_t newbrk) {
    return syscall1(SYS_brk, newbrk);
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
sys_clock_gettime(int clock_id, struct timespec *tp) {
    return syscall2(SYS_clock_gettime, clock_id, (uintptr_t)tp);
}

int
sys_clock_getres(int clock_id, struct timespec *res) {
    return syscall2(SYS_clock_getres, clock_id, (uintptr_t)res);
}

int
sys_gettimeofday(struct timeval *tv, struct timezone *tz) {
    return syscall2(SYS_gettimeofday, (uintptr_t)tv, (uintptr_t)tz);
}

int
sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    return syscall2(SYS_nanosleep, (uintptr_t)req, (uintptr_t)rem);
}

int
sys_time(int32_t *store) {
    return syscall1(SYS_time, (uintptr_t)store);
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
sys_stat(const char *path, struct stat *stat) {
    return syscall2(SYS_stat, (uintptr_t)path, (uintptr_t)stat);
}

int
sys_lstat(const char *path, struct stat *stat) {
    return syscall2(SYS_lstat, (uintptr_t)path, (uintptr_t)stat);
}

int
sys_fsync(int fd) {
    return syscall1(SYS_fsync, fd);
}

int
sys_ftruncate(int fd, off_t length) {
    return syscall2(SYS_ftruncate, fd, length);
}

int
sys_truncate(const char *path, off_t length) {
    return syscall2(SYS_truncate, (uintptr_t)path, length);
}

int
sys_pread(int fd, void *base, size_t len, off_t offset) {
    return syscall4(SYS_pread, fd, (uintptr_t)base, len, offset);
}

int
sys_pwrite(int fd, const void *base, size_t len, off_t offset) {
    return syscall4(SYS_pwrite, fd, (uintptr_t)base, len, offset);
}

int
sys_readv(int fd, const struct iovec *iov, size_t count) {
    return syscall3(SYS_readv, fd, (uintptr_t)iov, count);
}

int
sys_writev(int fd, const struct iovec *iov, size_t count) {
    return syscall3(SYS_writev, fd, (uintptr_t)iov, count);
}

int
sys_chdir(const char *path) {
    return syscall1(SYS_chdir, (uintptr_t)path);
}

int
sys_fchdir(int fd) {
    return syscall1(SYS_fchdir, fd);
}

int
sys_mkdir(const char *path) {
    return syscall1(SYS_mkdir, (uintptr_t)path);
}

int
sys_link(const char *old_path, const char *new_path) {
    return syscall2(SYS_link, (uintptr_t)old_path, (uintptr_t)new_path);
}

int
sys_unlink(const char *path) {
    return syscall1(SYS_unlink, (uintptr_t)path);
}

int
sys_rmdir(const char *path) {
    return syscall1(SYS_rmdir, (uintptr_t)path);
}

int
sys_rename(const char *old_path, const char *new_path) {
    return syscall2(SYS_rename, (uintptr_t)old_path, (uintptr_t)new_path);
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

int
sys_pipe(int fd[2]) {
    return syscall1(SYS_pipe, (uintptr_t)fd);
}

int
sys_pipe2(int fd[2], uint32_t flags) {
    return syscall2(SYS_pipe2, (uintptr_t)fd, flags);
}
