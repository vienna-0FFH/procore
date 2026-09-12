#ifndef __KERN_FS_SYSFILE_H__
#define __KERN_FS_SYSFILE_H__

#include <defs.h>

struct stat;
struct dirent;
struct iovec;

int sysfile_open(const char *path, uint32_t open_flags);        // Open or create a file. FLAGS/MODE per the syscall.
int sysfile_close(int fd);                                      // Close a vnode opened  
int sysfile_read(int fd, void *base, size_t len);               // Read file
int sysfile_write(int fd, void *base, size_t len);              // Write file
int sysfile_seek(int fd, off_t pos, int whence);                // Seek file  
int sysfile_fstat(int fd, struct stat *stat);                   // Stat file 
int sysfile_stat(const char *path, struct stat *stat, bool nofollow);
int sysfile_fsync(int fd);                                      // Sync file
int sysfile_ftruncate(int fd, off_t length);
int sysfile_truncate(const char *path, off_t length);
int sysfile_pread(int fd, void *base, size_t len, off_t offset);
int sysfile_pwrite(int fd, const void *base, size_t len, off_t offset);
int sysfile_readv(int fd, const struct iovec *iov, size_t count);
int sysfile_writev(int fd, const struct iovec *iov, size_t count);
int sysfile_chdir(const char *path);                            // change DIR  
int sysfile_fchdir(int fd);
int sysfile_mkdir(const char *path);                            // create DIR
int sysfile_link(const char *path1, const char *path2);         // set a path1's link as path2
int sysfile_rename(const char *path1, const char *path2);       // rename file
int sysfile_unlink(const char *path);                           // unlink a path
int sysfile_rmdir(const char *path);
int sysfile_getcwd(char *buf, size_t len);                      // get current working directory
int sysfile_getdirentry(int fd, struct dirent *direntp);        // get the file entry in DIR 
int sysfile_dup(int fd1, int fd2);                              // duplicate file
int sysfile_pipe(int *fd_store);                                // build PIPE   
int sysfile_pipe2(int *fd_store, uint32_t flags);
int sysfile_socketpair(int domain, int type, int protocol, int *fd_store);
int sysfile_symlink(const char *target, const char *link_path);
int sysfile_readlink(const char *path, char *buffer, size_t len);
int sysfile_mkfifo(const char *name, uint32_t open_flags);      // build named PIPE

#endif /* !__KERN_FS_SYSFILE_H__ */
