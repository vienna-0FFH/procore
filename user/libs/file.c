#include <defs.h>
#include <string.h>
#include <syscall.h>
#include <stdio.h>
#include <stat.h>
#include <error.h>
#include <unistd.h>

int
open(const char *path, uint32_t open_flags) {
    return sys_open(path, open_flags);
}

int
close(int fd) {
    return sys_close(fd);
}

int
read(int fd, void *base, size_t len) {
    return sys_read(fd, base, len);
}

int
write(int fd, void *base, size_t len) {
    return sys_write(fd, base, len);
}

int
seek(int fd, off_t pos, int whence) {
    return sys_seek(fd, pos, whence);
}

int
lseek(int fd, off_t pos, int whence) {
    return seek(fd, pos, whence);
}

int
fstat(int fd, struct stat *stat) {
    return sys_fstat(fd, stat);
}

int
stat(const char *path, struct stat *stat) {
    return sys_stat(path, stat);
}

int
lstat(const char *path, struct stat *stat) {
    return sys_lstat(path, stat);
}

int
fsync(int fd) {
    return sys_fsync(fd);
}

int
ftruncate(int fd, off_t length) {
    return sys_ftruncate(fd, length);
}

int
truncate(const char *path, off_t length) {
    return sys_truncate(path, length);
}

int
pread(int fd, void *base, size_t len, off_t offset) {
    return sys_pread(fd, base, len, offset);
}

int
pwrite(int fd, const void *base, size_t len, off_t offset) {
    return sys_pwrite(fd, base, len, offset);
}

int
readv(int fd, const struct iovec *iov, size_t count) {
    return sys_readv(fd, iov, count);
}

int
writev(int fd, const struct iovec *iov, size_t count) {
    return sys_writev(fd, iov, count);
}

int
dup2(int fd1, int fd2) {
    return sys_dup(fd1, fd2);
}

int
dup(int fd) {
    return sys_dup(fd, NO_FD);
}

int
pipe(int fd[2]) {
    return sys_pipe(fd);
}

int
pipe2(int fd[2], uint32_t flags) {
    return sys_pipe2(fd, flags);
}

int
fcntl(int fd, int command, uint32_t argument) {
    return sys_fcntl(fd, command, argument);
}

static char
transmode(struct stat *stat) {
    uint32_t mode = stat->st_mode;
    if (S_ISREG(mode)) return 'r';
    if (S_ISDIR(mode)) return 'd';
    if (S_ISLNK(mode)) return 'l';
    if (S_ISCHR(mode)) return 'c';
    if (S_ISBLK(mode)) return 'b';
    return '-';
}

void
print_stat(const char *name, int fd, struct stat *stat) {
    cprintf("[%03d] %s\n", fd, name);
    cprintf("    mode    : %c\n", transmode(stat));
    cprintf("    links   : %lu\n", stat->st_nlinks);
    cprintf("    blocks  : %lu\n", stat->st_blocks);
    cprintf("    size    : %lu\n", stat->st_size);
}
