#ifndef __UCORE_TCC_UNISTD_H__
#define __UCORE_TCC_UNISTD_H__

#include <defs.h>
#include <unistd.h>

typedef intptr_t ssize_t;

#ifndef UCORE_FD_SETSIZE
#define UCORE_FD_SETSIZE 256
#endif
#define UCORE_FD_SET_WORDS ((UCORE_FD_SETSIZE + 31) / 32)
typedef struct { uint32_t bits[UCORE_FD_SET_WORDS]; } fd_set;
#define FD_ZERO(set) do { size_t __fd_i; for (__fd_i = 0; __fd_i < UCORE_FD_SET_WORDS; __fd_i++) (set)->bits[__fd_i] = 0; } while (0)
#define FD_SET(fd, set) do { if ((fd) >= 0 && (fd) < UCORE_FD_SETSIZE) (set)->bits[(fd) >> 5] |= 1U << ((fd) & 31); } while (0)
#define FD_CLR(fd, set) do { if ((fd) >= 0 && (fd) < UCORE_FD_SETSIZE) (set)->bits[(fd) >> 5] &= ~(1U << ((fd) & 31)); } while (0)
#define FD_ISSET(fd, set) ((fd) >= 0 && (fd) < UCORE_FD_SETSIZE && (((set)->bits[(fd) >> 5] & (1U << ((fd) & 31))) != 0))

#ifndef __UCORE_TIMEVAL_DEFINED
#define __UCORE_TIMEVAL_DEFINED
struct timeval { int tv_sec; int tv_usec; };
#endif

#ifndef __UCORE_UTSNAME_DEFINED
#define __UCORE_UTSNAME_DEFINED
#define UCORE_UTS_FIELD_LEN 65
struct utsname {
    char sysname[UCORE_UTS_FIELD_LEN];
    char nodename[UCORE_UTS_FIELD_LEN];
    char release[UCORE_UTS_FIELD_LEN];
    char version[UCORE_UTS_FIELD_LEN];
    char machine[UCORE_UTS_FIELD_LEN];
    char domainname[UCORE_UTS_FIELD_LEN];
};
#endif

#ifndef __UCORE_SYSINFO_DEFINED
#define __UCORE_SYSINFO_DEFINED
struct sysinfo {
    int32_t uptime;
    uint32_t loads[3];
    uint32_t totalram;
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
    char _f[8];
};
#endif

int open(const char *, int, ...);
int close(int);
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
int lseek(int, off_t, int);
int unlink(const char *);
int access(const char *, int);
int chdir(const char *);
char *getcwd(char *, size_t);
int execvp(const char *, char *const []);
int wait4(int, int *, unsigned int);
int fchdir(int);
int rmdir(const char *);
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int uname(struct utsname *);
int sysinfo(struct sysinfo *);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif
