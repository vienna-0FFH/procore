#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <proc.h>
#include <file.h>
#include <unistd.h>
#include <iobuf.h>
#include <inode.h>
#include <stat.h>
#include <dirent.h>
#include <error.h>
#include <assert.h>
#include <net.h>

#define testfd(fd)                          ((fd) >= 0 && (fd) < FILES_STRUCT_NENTRY)

// get_fd_array - get current process's open files table
static struct file *
get_fd_array(void) {
    struct files_struct *filesp = current->filesp;
    assert(filesp != NULL && files_count(filesp) > 0);
    return filesp->fd_array;
}

// fd_array_init - initialize the open files table
void
fd_array_init(struct file *fd_array) {
    int fd;
    struct file *file = fd_array;
    for (fd = 0; fd < FILES_STRUCT_NENTRY; fd ++, file ++) {
        file->open_count = 0;
        file->status = FD_NONE, file->fd = fd;
        file->pos = 0;
        file->object.node = NULL;
    }
}

// fs_array_alloc - allocate a free file item (with FD_NONE status) in open files table
static int
fd_array_alloc(int fd, struct file **file_store) {
//    panic("debug");
    struct file *file = get_fd_array();
    if (fd == NO_FD) {
        for (fd = 0; fd < FILES_STRUCT_NENTRY; fd ++, file ++) {
            if (file->status == FD_NONE) {
                goto found;
            }
        }
        return -E_MAX_OPEN;
    }
    else {
        if (testfd(fd)) {
            file += fd;
            if (file->status == FD_NONE) {
                goto found;
            }
            return -E_BUSY;
        }
        return -E_INVAL;
    }
found:
    assert(fopen_count(file) == 0);
    file->status = FD_INIT, file->object.node = NULL;
    *file_store = file;
    return 0;
}

// fd_array_free - free a file item in open files table
static void
fd_array_free(struct file *file) {
    assert(file->status == FD_INIT || file->status == FD_CLOSED);
    assert(fopen_count(file) == 0);
    if (file->status == FD_CLOSED) {
        if (file_is_socket(file)) {
            net_socket_put(file_socket(file));
        }
        else {
            vfs_close(file_node(file));
        }
    }
    file->object.node = NULL;
    file->status = FD_NONE;
}

static void
fd_array_acquire(struct file *file) {
    assert(file->status == FD_OPENED);
    fopen_count_inc(file);
}

// fd_array_release - file's open_count--; if file's open_count-- == 0 , then call fd_array_free to free this file item
static void
fd_array_release(struct file *file) {
    assert(file->status == FD_OPENED || file->status == FD_CLOSED);
    assert(fopen_count(file) > 0);
    if (fopen_count_dec(file) == 0) {
        fd_array_free(file);
    }
}

// fd_array_open - file's open_count++, set status to FD_OPENED
void
fd_array_open(struct file *file) {
    assert(file->status == FD_INIT &&
           (file_node(file) != NULL || file_is_socket(file)));
    file->status = FD_OPENED;
    fopen_count_inc(file);
}

// fd_array_close - file's open_count--; if file's open_count-- == 0 , then call fd_array_free to free this file item
void
fd_array_close(struct file *file) {
    assert(file->status == FD_OPENED);
    assert(fopen_count(file) > 0);
    if (file_is_socket(file)) {
        net_socket_close_descriptor(file_socket(file));
    }
    file->status = FD_CLOSED;
    if (fopen_count_dec(file) == 0) {
        fd_array_free(file);
    }
}

//fs_array_dup - duplicate file 'from'  to file 'to'
void
fd_array_dup(struct file *to, struct file *from) {
    //cprintf("[fd_array_dup]from fd=%d, to fd=%d\n",from->fd, to->fd);
    assert(to->status == FD_INIT && from->status == FD_OPENED);
    to->pos = from->pos;
    to->readable = from->readable;
    to->writable = from->writable;
    if (file_is_socket(from)) {
        file_socket(to) = file_socket(from);
        net_socket_get_descriptor(file_socket(to));
    }
    else {
        struct inode *node = file_node(from);
        vop_ref_inc(node), vop_open_inc(node);
        file_node(to) = node;
    }
    fd_array_open(to);
}

// fd2file - use fd as index of fd_array, return the array item (file)
static inline int
fd2file(int fd, struct file **file_store) {
    if (testfd(fd)) {
        struct file *file = get_fd_array() + fd;
        if (file->status == FD_OPENED && file->fd == fd) {
            *file_store = file;
            return 0;
        }
    }
    return -E_INVAL;
}

// file_testfd - test file is readble or writable?
bool
file_testfd(int fd, bool readable, bool writable) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return 0;
    }
    if (readable && !file->readable) {
        return 0;
    }
    if (writable && !file->writable) {
        return 0;
    }
    return 1;
}

// open file
int
file_open(char *path, uint32_t open_flags) {
    bool readable = 0, writable = 0;
    switch (open_flags & O_ACCMODE) {
    case O_RDONLY: readable = 1; break;
    case O_WRONLY: writable = 1; break;
    case O_RDWR:
        readable = writable = 1;
        break;
    default:
        return -E_INVAL;
    }

    int ret;
    struct file *file;
    if ((ret = fd_array_alloc(NO_FD, &file)) != 0) {
        return ret;
    }

    struct inode *node;
    if ((ret = vfs_open(path, open_flags, &node)) != 0) {
        fd_array_free(file);
        return ret;
    }

    file->pos = 0;
    if (open_flags & O_APPEND) {
        struct stat __stat, *stat = &__stat;
        if ((ret = vop_fstat(node, stat)) != 0) {
            vfs_close(node);
            fd_array_free(file);
            return ret;
        }
        file->pos = stat->st_size;
    }

    file_node(file) = node;
    file->readable = readable;
    file->writable = writable;
    fd_array_open(file);
    return file->fd;
}

// close file
int
file_close(int fd) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    fd_array_close(file);
    return 0;
}

// read file
int
file_read(int fd, void *base, size_t len, size_t *copied_store) {
    int ret;
    struct file *file;
    *copied_store = 0;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (!file->readable) {
        return -E_INVAL;
    }
    if (file_is_socket(file)) {
        return -E_INVAL;
    }
    fd_array_acquire(file);

    struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
    ret = vop_read(file_node(file), iob);

    size_t copied = iobuf_used(iob);
    if (file->status == FD_OPENED) {
        file->pos += copied;
    }
    *copied_store = copied;
    fd_array_release(file);
    return ret;
}

// write file
int
file_write(int fd, void *base, size_t len, size_t *copied_store) {
    int ret;
    struct file *file;
    *copied_store = 0;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (!file->writable) {
        return -E_INVAL;
    }
    if (file_is_socket(file)) {
        return -E_INVAL;
    }
    fd_array_acquire(file);

    struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
    ret = vop_write(file_node(file), iob);

    size_t copied = iobuf_used(iob);
    if (file->status == FD_OPENED) {
        file->pos += copied;
    }
    *copied_store = copied;
    fd_array_release(file);
    return ret;
}

// seek file
int
file_seek(int fd, off_t pos, int whence) {
    struct stat __stat, *stat = &__stat;
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (file_is_socket(file)) {
        return -E_SEEK;
    }
    fd_array_acquire(file);

    switch (whence) {
    case LSEEK_SET: break;
    case LSEEK_CUR: pos += file->pos; break;
    case LSEEK_END:
        if ((ret = vop_fstat(file_node(file), stat)) == 0) {
            pos += stat->st_size;
        }
        break;
    default: ret = -E_INVAL;
    }

    if (ret == 0) {
        if ((ret = vop_tryseek(file_node(file), pos)) == 0) {
            file->pos = pos;
        }
//    cprintf("file_seek, pos=%d, whence=%d, ret=%d\n", pos, whence, ret);
    }
    fd_array_release(file);
    return ret;
}

// stat file
int
file_fstat(int fd, struct stat *stat) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (file_is_socket(file)) {
        return -E_INVAL;
    }
    fd_array_acquire(file);
    ret = vop_fstat(file_node(file), stat);
    fd_array_release(file);
    return ret;
}

// sync file
int
file_fsync(int fd) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (file_is_socket(file)) {
        return -E_INVAL;
    }
    fd_array_acquire(file);
    ret = vop_fsync(file_node(file));
    fd_array_release(file);
    return ret;
}

// get file entry in DIR
int
file_getdirentry(int fd, struct dirent *direntp) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (file_is_socket(file)) {
        return -E_NOTDIR;
    }
    fd_array_acquire(file);

    struct iobuf __iob, *iob = iobuf_init(&__iob, direntp->name, sizeof(direntp->name), direntp->offset);
    if ((ret = vop_getdirentry(file_node(file), iob)) == 0) {
        direntp->offset += iobuf_used(iob);
    }
    fd_array_release(file);
    return ret;
}

// duplicate file
int
file_dup(int fd1, int fd2) {
    int ret;
    struct file *file1, *file2;
    if ((ret = fd2file(fd1, &file1)) != 0) {
        return ret;
    }
    if ((ret = fd_array_alloc(fd2, &file2)) != 0) {
        return ret;
    }
    fd_array_dup(file2, file1);
    return file2->fd;
}

int
file_socket_create(int domain, int type, int protocol) {
    struct file *file;
    struct net_socket *socket;
    int ret;

    if (domain != AF_INET || type != SOCK_DGRAM ||
        (protocol != 0 && protocol != IPPROTO_UDP)) {
        return -E_INVAL;
    }
    if ((ret = fd_array_alloc(NO_FD, &file)) != 0) {
        return ret;
    }
    socket = net_socket_create(domain, type, protocol);
    if (socket == NULL) {
        fd_array_free(file);
        return -E_NO_MEM;
    }
    file->readable = 1;
    file->writable = 1;
    file->pos = FILE_SOCKET_POS;
    file_socket(file) = socket;
    fd_array_open(file);
    return file->fd;
}

static int
file_socket_get(int fd, struct file **file_store) {
    int ret = fd2file(fd, file_store);
    if (ret != 0) {
        return ret;
    }
    if (!file_is_socket(*file_store) || file_socket(*file_store) == NULL) {
        return -E_INVAL;
    }
    return 0;
}

int
file_socket_bind(int fd, const struct sockaddr_in *address, size_t length) {
    struct file *file;
    int ret = file_socket_get(fd, &file);
    if (ret == 0) {
        fd_array_acquire(file);
        ret = net_socket_bind(file_socket(file), address, length);
        fd_array_release(file);
    }
    return ret;
}

int
file_socket_sendto(int fd, const void *data, size_t length,
                   const struct sockaddr_in *destination, size_t dest_length) {
    struct file *file;
    int ret = file_socket_get(fd, &file);
    if (ret == 0) {
        fd_array_acquire(file);
        ret = net_socket_sendto(file_socket(file), data, length,
                                destination, dest_length);
        fd_array_release(file);
    }
    return ret;
}

int
file_socket_recvfrom(int fd, void *data, size_t length,
                     struct sockaddr_in *source, size_t *source_length) {
    struct file *file;
    int ret = file_socket_get(fd, &file);
    if (ret == 0) {
        fd_array_acquire(file);
        ret = net_socket_recvfrom(file_socket(file), data, length,
                                  source, source_length);
        fd_array_release(file);
    }
    return ret;
}
