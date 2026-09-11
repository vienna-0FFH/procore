#ifndef __UCORE_TCC_UNISTD_H__
#define __UCORE_TCC_UNISTD_H__

#include <defs.h>
#include <unistd.h>

typedef intptr_t ssize_t;

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
