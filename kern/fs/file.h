#ifndef __KERN_FS_FILE_H__
#define __KERN_FS_FILE_H__

#include <defs.h>
#include <fs.h>

struct inode;
struct stat;
struct dirent;
struct net_socket;
struct sockaddr_in;
struct open_file;

/* A file is an FD-table slot. The referenced open_file is shared by dup()
 * and fork(), matching the POSIX open-file-description lifetime model. */
struct file {
    enum {
        FD_NONE, FD_INIT, FD_OPENED,
    } status;
    int fd;
    uint32_t fd_flags;
    struct open_file *description;
};

void fd_array_init(struct file *fd_array);
struct open_file *fd_array_close(struct file *file);
void fd_array_dup(struct file *to, const struct file *from);
void open_file_put(struct open_file *description);

bool file_testfd(int fd, bool readable, bool writable);
int file_open(char *path, uint32_t open_flags);
int file_close(int fd);
int file_read(int fd, void *base, size_t len, size_t *copied_store);
int file_write(int fd, void *base, size_t len, size_t *copied_store);
int file_seek(int fd, off_t pos, int whence);
int file_fstat(int fd, struct stat *stat);
int file_fsync(int fd);
int file_getdirentry(int fd, struct dirent *dirent);
int file_dup(int fd1, int fd2);
int file_fcntl(int fd, int command, uint32_t argument);
int file_poll(int fd, int16_t events, int16_t *revents_store);
int file_pipe(int fd[]);
int file_mkfifo(const char *name, uint32_t open_flags);
int file_socket_create(int domain, int type, int protocol);
int file_socket_bind(int fd, const struct sockaddr_in *address, size_t length);
int file_socket_connect(int fd, const struct sockaddr_in *address, size_t length);
int file_socket_listen(int fd, int backlog);
int file_socket_accept(int fd, struct sockaddr_in *address, bool nonblock);
int file_socket_shutdown(int fd, int how);
int file_socket_sendto(int fd, const void *data, size_t length,
                       const struct sockaddr_in *destination, size_t dest_length);
int file_socket_recvfrom(int fd, void *data, size_t length,
                         struct sockaddr_in *source, size_t *source_length);
int file_socket_send(int fd, const void *data, size_t length);
int file_socket_recv(int fd, void *data, size_t length);
int file_socket_getsockname(int fd, struct sockaddr_in *address);
int file_socket_getpeername(int fd, struct sockaddr_in *address);

#endif /* !__KERN_FS_FILE_H__ */
