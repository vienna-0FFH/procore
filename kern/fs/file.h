#ifndef __KERN_FS_FILE_H__
#define __KERN_FS_FILE_H__

//#include <types.h>
#include <fs.h>
#include <proc.h>
#include <atomic.h>
#include <assert.h>
#include <unistd.h>

struct inode;
struct stat;
struct dirent;
struct net_socket;
struct sockaddr_in;

struct file {
    enum {
        FD_NONE, FD_INIT, FD_OPENED, FD_CLOSED,
    } status;
    bool readable;
    bool writable;
    int fd;
    off_t pos;
    union {
        struct inode *node;
        struct net_socket *socket;
    } object;
    volatile int open_count;
};

void fd_array_init(struct file *fd_array);
void fd_array_open(struct file *file);
void fd_array_close(struct file *file);
void fd_array_dup(struct file *to, struct file *from);
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
int file_pipe(int fd[]);
int file_mkfifo(const char *name, uint32_t open_flags);
int file_socket_create(int domain, int type, int protocol);
int file_socket_bind(int fd, const struct sockaddr_in *address, size_t length);
int file_socket_sendto(int fd, const void *data, size_t length,
                       const struct sockaddr_in *destination, size_t dest_length);
int file_socket_recvfrom(int fd, void *data, size_t length,
                         struct sockaddr_in *source, size_t *source_length);

#define file_node(file)       ((file)->object.node)
#define file_socket(file)     ((file)->object.socket)
#define FILE_SOCKET_POS       ((off_t)-1)
#define file_is_socket(file)  ((file)->pos == FILE_SOCKET_POS)

static inline int
fopen_count(struct file *file) {
    return file->open_count;
}

static inline int
fopen_count_inc(struct file *file) {
    return atomic_inc_return(&file->open_count);
}

static inline int
fopen_count_dec(struct file *file) {
    return atomic_dec_return(&file->open_count);
}

#endif /* !__KERN_FS_FILE_H__ */
