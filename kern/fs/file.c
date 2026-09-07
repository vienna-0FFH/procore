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
#include <atomic.h>
#include <sem.h>
#include <net.h>
#include <kmalloc.h>

#define testfd(fd)              ((fd) >= 0 && (fd) < FILES_STRUCT_NENTRY)

enum open_file_kind {
    OPEN_FILE_INODE,
    OPEN_FILE_SOCKET,
};

/*
 * One open-file description may be referenced by several descriptors and by
 * in-flight operations. descriptor_count controls socket close notification;
 * ref_count controls allocation and inode/socket lifetime.
 */
struct open_file {
    volatile int ref_count;
    volatile int descriptor_count;
    enum open_file_kind kind;
    bool readable;
    bool writable;
    bool append;
    off_t pos;
    union {
        struct inode *node;
        struct net_socket *socket;
    } object;
    semaphore_t operation_sem;
};

void open_file_put(struct open_file *description);

static struct files_struct *
current_files(void) {
    struct files_struct *filesp = current->filesp;
    assert(filesp != NULL && files_count(filesp) > 0);
    return filesp;
}

static struct open_file *
open_file_create(enum open_file_kind kind, bool readable, bool writable,
                 bool append, off_t pos, void *object) {
    struct open_file *description = kmalloc(sizeof(*description));
    if (description == NULL) {
        return NULL;
    }
    description->ref_count = 1;
    description->descriptor_count = 1;
    description->kind = kind;
    description->readable = readable;
    description->writable = writable;
    description->append = append;
    description->pos = pos;
    description->object.node = object;
    sem_init(&description->operation_sem, 1);
    return description;
}

static void
open_file_get(struct open_file *description) {
    assert(description != NULL && description->ref_count > 0);
    assert(atomic_inc_return(&description->ref_count) > 1);
}

static void
open_file_get_descriptor(struct open_file *description) {
    open_file_get(description);
    assert(description->descriptor_count > 0);
    assert(atomic_inc_return(&description->descriptor_count) > 1);
}

static void
open_file_drop_descriptor(struct open_file *description) {
    int descriptors;
    assert(description != NULL && description->descriptor_count > 0);
    descriptors = atomic_dec_return(&description->descriptor_count);
    assert(descriptors >= 0);
    if (descriptors == 0 && description->kind == OPEN_FILE_SOCKET) {
        net_socket_close_descriptor(description->object.socket);
    }
}

void
open_file_put(struct open_file *description) {
    int references;
    if (description == NULL) {
        return;
    }
    references = atomic_dec_return(&description->ref_count);
    assert(references >= 0);
    if (references != 0) {
        return;
    }
    assert(description->descriptor_count == 0);
    if (description->kind == OPEN_FILE_SOCKET) {
        net_socket_put(description->object.socket);
    }
    else {
        vfs_close(description->object.node);
    }
    kfree(description);
}

void
fd_array_init(struct file *fd_array) {
    int fd;
    for (fd = 0; fd < FILES_STRUCT_NENTRY; fd ++) {
        fd_array[fd].status = FD_NONE;
        fd_array[fd].fd = fd;
        fd_array[fd].description = NULL;
    }
}

/* Caller holds files_sem. */
static int
fd_array_alloc(struct files_struct *filesp, int requested,
               struct file **file_store) {
    int fd;
    if (requested == NO_FD) {
        for (fd = 0; fd < FILES_STRUCT_NENTRY; fd ++) {
            if (filesp->fd_array[fd].status == FD_NONE) {
                goto found;
            }
        }
        return -E_MAX_OPEN;
    }
    if (!testfd(requested)) {
        return -E_INVAL;
    }
    fd = requested;
    if (filesp->fd_array[fd].status != FD_NONE) {
        return -E_BUSY;
    }

found:
    filesp->fd_array[fd].status = FD_INIT;
    filesp->fd_array[fd].description = NULL;
    *file_store = &filesp->fd_array[fd];
    return 0;
}

/* Caller holds files_sem. */
static void
fd_array_cancel(struct file *file) {
    assert(file->status == FD_INIT && file->description == NULL);
    file->status = FD_NONE;
}

/* Caller holds files_sem and transfers one descriptor reference to the slot. */
static void
fd_array_install(struct file *file, struct open_file *description) {
    assert(file->status == FD_INIT && file->description == NULL);
    assert(description != NULL && description->descriptor_count > 0);
    file->description = description;
    file->status = FD_OPENED;
}

/* Caller holds files_sem. The returned reference must be put after unlock. */
struct open_file *
fd_array_close(struct file *file) {
    struct open_file *description;
    assert(file->status == FD_OPENED && file->description != NULL);
    description = file->description;
    file->description = NULL;
    file->status = FD_NONE;
    open_file_drop_descriptor(description);
    return description;
}

/* Caller holds the source table lock; TO must not be externally visible. */
void
fd_array_dup(struct file *to, const struct file *from) {
    struct open_file *description;
    assert(to != from && (to->status == FD_NONE || to->status == FD_INIT));
    assert(from->status == FD_OPENED && from->description != NULL);
    description = from->description;
    open_file_get_descriptor(description);
    to->description = description;
    to->status = FD_OPENED;
}

/* Caller holds files_sem. */
static int
fd2file_locked(struct files_struct *filesp, int fd,
               struct file **file_store) {
    if (testfd(fd)) {
        struct file *file = &filesp->fd_array[fd];
        if (file->status == FD_OPENED && file->fd == fd &&
            file->description != NULL) {
            *file_store = file;
            return 0;
        }
    }
    return -E_INVAL;
}

static int
fd_acquire(int fd, struct open_file **description_store) {
    struct files_struct *filesp = current_files();
    struct file *file;
    int ret;

    lock_files(filesp);
    ret = fd2file_locked(filesp, fd, &file);
    if (ret == 0) {
        open_file_get(file->description);
        *description_store = file->description;
    }
    unlock_files(filesp);
    return ret;
}

bool
file_testfd(int fd, bool readable, bool writable) {
    struct files_struct *filesp = current_files();
    struct file *file;
    bool valid = 0;

    lock_files(filesp);
    if (fd2file_locked(filesp, fd, &file) == 0) {
        struct open_file *description = file->description;
        valid = (!readable || description->readable) &&
                (!writable || description->writable);
    }
    unlock_files(filesp);
    return valid;
}

int
file_open(char *path, uint32_t open_flags) {
    struct files_struct *filesp = current_files();
    struct open_file *description;
    struct file *file;
    struct inode *node;
    bool readable = 0, writable = 0;
    bool append = (open_flags & O_APPEND) != 0;
    off_t pos = 0;
    int ret;

    switch (open_flags & O_ACCMODE) {
    case O_RDONLY: readable = 1; break;
    case O_WRONLY: writable = 1; break;
    case O_RDWR: readable = writable = 1; break;
    default: return -E_INVAL;
    }

    /* Reserve a descriptor before pathname creation so EMFILE cannot leave
     * behind an O_CREAT inode. FD_INIT is invisible to normal lookups and is
     * cancelled on every failure path below. */
    lock_files(filesp);
    ret = fd_array_alloc(filesp, NO_FD, &file);
    unlock_files(filesp);
    if (ret != 0) {
        return ret;
    }

    if ((ret = vfs_open(path, open_flags, &node)) != 0) {
        goto failed_slot;
    }
    if (append) {
        struct stat stat;
        if ((ret = vop_fstat(node, &stat)) != 0) {
            vfs_close(node);
            goto failed_slot;
        }
        pos = stat.st_size;
    }
    description = open_file_create(OPEN_FILE_INODE, readable, writable,
                                   append, pos, node);
    if (description == NULL) {
        vfs_close(node);
        ret = -E_NO_MEM;
        goto failed_slot;
    }

    lock_files(filesp);
    fd_array_install(file, description);
    unlock_files(filesp);
    return file->fd;

failed_slot:
    lock_files(filesp);
    fd_array_cancel(file);
    unlock_files(filesp);
    return ret;
}

int
file_close(int fd) {
    struct files_struct *filesp = current_files();
    struct open_file *description = NULL;
    struct file *file;
    int ret;

    lock_files(filesp);
    if ((ret = fd2file_locked(filesp, fd, &file)) == 0) {
        description = fd_array_close(file);
    }
    unlock_files(filesp);
    if (ret != 0) {
        return ret;
    }
    open_file_put(description);
    return 0;
}

static int
regular_file_acquire(int fd, bool readable, bool writable,
                     struct open_file **description_store) {
    struct open_file *description;
    int ret = fd_acquire(fd, &description);
    if (ret != 0) {
        return ret;
    }
    if (description->kind != OPEN_FILE_INODE ||
        (readable && !description->readable) ||
        (writable && !description->writable)) {
        open_file_put(description);
        return -E_INVAL;
    }
    *description_store = description;
    return 0;
}

int
file_read(int fd, void *base, size_t len, size_t *copied_store) {
    struct open_file *description;
    struct iobuf iob;
    size_t copied;
    int ret;

    *copied_store = 0;
    if ((ret = regular_file_acquire(fd, 1, 0, &description)) != 0) {
        return ret;
    }
    down(&description->operation_sem);
    iobuf_init(&iob, base, len, description->pos);
    ret = vop_read(description->object.node, &iob);
    copied = iobuf_used(&iob);
    description->pos += copied;
    up(&description->operation_sem);
    *copied_store = copied;
    open_file_put(description);
    return ret;
}

int
file_write(int fd, void *base, size_t len, size_t *copied_store) {
    struct open_file *description;
    struct iobuf iob;
    size_t copied;
    int ret;

    *copied_store = 0;
    if ((ret = regular_file_acquire(fd, 0, 1, &description)) != 0) {
        return ret;
    }
    down(&description->operation_sem);
    if (description->append) {
        struct stat stat;
        ret = vop_fstat(description->object.node, &stat);
        if (ret != 0) {
            up(&description->operation_sem);
            open_file_put(description);
            return ret;
        }
        description->pos = stat.st_size;
    }
    iobuf_init(&iob, base, len, description->pos);
    ret = vop_write(description->object.node, &iob);
    copied = iobuf_used(&iob);
    description->pos += copied;
    up(&description->operation_sem);
    *copied_store = copied;
    open_file_put(description);
    return ret;
}

int
file_seek(int fd, off_t pos, int whence) {
    struct open_file *description;
    int ret = fd_acquire(fd, &description);
    if (ret != 0) {
        return ret;
    }
    if (description->kind != OPEN_FILE_INODE) {
        open_file_put(description);
        return -E_SEEK;
    }

    down(&description->operation_sem);
    switch (whence) {
    case LSEEK_SET:
        break;
    case LSEEK_CUR:
        pos += description->pos;
        break;
    case LSEEK_END: {
        struct stat stat;
        if ((ret = vop_fstat(description->object.node, &stat)) == 0) {
            pos += stat.st_size;
        }
        break;
    }
    default:
        ret = -E_INVAL;
    }
    if (ret == 0 &&
        (ret = vop_tryseek(description->object.node, pos)) == 0) {
        description->pos = pos;
        ret = pos;
    }
    up(&description->operation_sem);
    open_file_put(description);
    return ret;
}

int
file_fstat(int fd, struct stat *stat) {
    struct open_file *description;
    int ret;
    if ((ret = regular_file_acquire(fd, 0, 0, &description)) != 0) {
        return ret;
    }
    down(&description->operation_sem);
    ret = vop_fstat(description->object.node, stat);
    up(&description->operation_sem);
    open_file_put(description);
    return ret;
}

int
file_fsync(int fd) {
    struct open_file *description;
    int ret;
    if ((ret = regular_file_acquire(fd, 0, 0, &description)) != 0) {
        return ret;
    }
    down(&description->operation_sem);
    ret = vop_fsync(description->object.node);
    up(&description->operation_sem);
    open_file_put(description);
    return ret;
}

int
file_getdirentry(int fd, struct dirent *direntp) {
    struct open_file *description;
    struct iobuf iob;
    int ret;
    if ((ret = regular_file_acquire(fd, 0, 0, &description)) != 0) {
        return ret;
    }
    down(&description->operation_sem);
    iobuf_init(&iob, direntp->name, sizeof(direntp->name), direntp->offset);
    if ((ret = vop_getdirentry(description->object.node, &iob)) == 0) {
        direntp->offset += iobuf_used(&iob);
    }
    up(&description->operation_sem);
    open_file_put(description);
    return ret;
}

int
file_dup(int fd1, int fd2) {
    struct files_struct *filesp = current_files();
    struct open_file *replaced = NULL;
    struct file *from, *to;
    int ret;

    lock_files(filesp);
    if ((ret = fd2file_locked(filesp, fd1, &from)) != 0) {
        goto out_unlock;
    }
    if (fd1 == fd2) {
        ret = fd1;
        goto out_unlock;
    }
    if (fd2 == NO_FD) {
        if ((ret = fd_array_alloc(filesp, NO_FD, &to)) != 0) {
            goto out_unlock;
        }
    }
    else {
        if (!testfd(fd2)) {
            ret = -E_INVAL;
            goto out_unlock;
        }
        to = &filesp->fd_array[fd2];
        if (to->status == FD_INIT) {
            ret = -E_BUSY;
            goto out_unlock;
        }
        if (to->status == FD_OPENED) {
            replaced = fd_array_close(to);
        }
    }
    fd_array_dup(to, from);
    ret = to->fd;

out_unlock:
    unlock_files(filesp);
    open_file_put(replaced);
    return ret;
}

int
file_socket_create(int domain, int type, int protocol) {
    struct files_struct *filesp = current_files();
    struct open_file *description;
    struct net_socket *socket;
    struct file *file;
    int ret;

    if (domain != AF_INET || type != SOCK_DGRAM ||
        (protocol != 0 && protocol != IPPROTO_UDP)) {
        return -E_INVAL;
    }

    lock_files(filesp);
    ret = fd_array_alloc(filesp, NO_FD, &file);
    unlock_files(filesp);
    if (ret != 0) {
        return ret;
    }

    socket = net_socket_create(domain, type, protocol);
    if (socket == NULL) {
        ret = -E_NO_MEM;
        goto failed_socket_slot;
    }
    description = open_file_create(OPEN_FILE_SOCKET, 1, 1, 0, 0, socket);
    if (description == NULL) {
        net_socket_close_descriptor(socket);
        net_socket_put(socket);
        ret = -E_NO_MEM;
        goto failed_socket_slot;
    }
    lock_files(filesp);
    fd_array_install(file, description);
    unlock_files(filesp);
    return file->fd;

failed_socket_slot:
    lock_files(filesp);
    fd_array_cancel(file);
    unlock_files(filesp);
    return ret;
}

static int
socket_file_acquire(int fd, struct open_file **description_store) {
    struct open_file *description;
    int ret = fd_acquire(fd, &description);
    if (ret != 0) {
        return ret;
    }
    if (description->kind != OPEN_FILE_SOCKET ||
        description->object.socket == NULL) {
        open_file_put(description);
        return -E_INVAL;
    }
    *description_store = description;
    return 0;
}

int
file_socket_bind(int fd, const struct sockaddr_in *address, size_t length) {
    struct open_file *description;
    int ret = socket_file_acquire(fd, &description);
    if (ret == 0) {
        ret = net_socket_bind(description->object.socket, address, length);
        open_file_put(description);
    }
    return ret;
}

int
file_socket_sendto(int fd, const void *data, size_t length,
                   const struct sockaddr_in *destination, size_t dest_length) {
    struct open_file *description;
    int ret = socket_file_acquire(fd, &description);
    if (ret == 0) {
        ret = net_socket_sendto(description->object.socket, data, length,
                                destination, dest_length);
        open_file_put(description);
    }
    return ret;
}

int
file_socket_recvfrom(int fd, void *data, size_t length,
                     struct sockaddr_in *source, size_t *source_length) {
    struct open_file *description;
    int ret = socket_file_acquire(fd, &description);
    if (ret == 0) {
        ret = net_socket_recvfrom(description->object.socket, data, length,
                                  source, source_length);
        open_file_put(description);
    }
    return ret;
}
