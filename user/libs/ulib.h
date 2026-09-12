#ifndef __USER_LIBS_ULIB_H__
#define __USER_LIBS_ULIB_H__

#include <defs.h>
#include <unistd.h>
#include <signal.h>

void __warn(const char *file, int line, const char *fmt, ...);
void __noreturn __panic(const char *file, int line, const char *fmt, ...);

#define warn(...)                                       \
    __warn(__FILE__, __LINE__, __VA_ARGS__)

#define panic(...)                                      \
    __panic(__FILE__, __LINE__, __VA_ARGS__)

#define assert(x)                                       \
    do {                                                \
        if (!(x)) {                                     \
            panic("assertion failed: %s", #x);          \
        }                                               \
    } while (0)

// static_assert(x) will generate a compile-time error if 'x' is false.
#define static_assert(x)                                \
    switch (x) { case 0: case (x): ; }

int fprintf(int fd, const char *fmt, ...);

void __noreturn exit(int error_code);
int fork(void);
int clone(int (*fn)(void *), void *child_stack,
          uint32_t clone_flags, void *arg);
int wait(void);
int waitpid(int pid, int *store);
int wait4(int pid, int *store, uint32_t options);
int waitid(int idtype, int id, siginfo_t *info, uint32_t options);
void yield(void);
int getpid(void);
int getppid(void);
int gettid(void);
int getcpu(void);
int uname(struct utsname *name);
int sysinfo(struct sysinfo *info);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int getresuid(uint32_t *real, uint32_t *effective, uint32_t *saved);
int getresgid(uint32_t *real, uint32_t *effective, uint32_t *saved);
int setaffinity(int pid, uint32_t mask);
int getaffinity(int pid, uint32_t *mask_store);
int getcpustat(int cpu, struct cpu_stat *stat);
void *mmap(void *addr, size_t len, uint32_t prot, uint32_t flags);
int munmap(void *addr, size_t len);
int mprotect(void *addr, size_t len, uint32_t prot);
int getrusage(int who, struct rusage *usage);
uintptr_t brk(uintptr_t newbrk);
void print_pgdir(void);
int sleep(unsigned int time);
unsigned int gettime_msec(void);
int clock_gettime(int clock_id, struct timespec *tp);
int clock_getres(int clock_id, struct timespec *res);
int gettimeofday(struct timeval *tv, struct timezone *tz);
int nanosleep(const struct timespec *req, struct timespec *rem);
int32_t time(int32_t *store);
int __exec(const char *name, const char **argv);

#define __exec0(name, path, ...)                \
({ const char *argv[] = {path, ##__VA_ARGS__, NULL}; __exec(name, argv); })

#define exec(path, ...)                         __exec0(NULL, path, ##__VA_ARGS__)
#define nexec(name, path, ...)                  __exec0(name, path, ##__VA_ARGS__)

void lab6_set_priority(uint32_t priority); //compatibility priority API

#endif /* !__USER_LIBS_ULIB_H__ */
