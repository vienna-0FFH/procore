#include <defs.h>
#include <string.h>
#include <vmm.h>
#include <proc.h>
#include <kmalloc.h>
#include <vfs.h>
#include <inode.h>
#include <file.h>
#include <iobuf.h>
#include <sysfile.h>
#include <stat.h>
#include <dirent.h>
#include <unistd.h>
#include <error.h>
#include <assert.h>
#include <fs_config.h>

#define IOBUF_SIZE                          4096
#define OFF_T_MAX                           ((off_t)~((uintptr_t)1 << 31))

/* copy_path - copy path name */
static int
copy_path(char **to, const char *from) {
    struct mm_struct *mm = current->mm;
    char *buffer;
    if ((buffer = kmalloc(FS_MAX_FPATH_LEN + 1)) == NULL) {
        return -E_NO_MEM;
    }
    lock_mm(mm);
    if (!copy_string(mm, buffer, from, FS_MAX_FPATH_LEN + 1)) {
        unlock_mm(mm);
        goto failed_cleanup;
    }
    unlock_mm(mm);
    *to = buffer;
    return 0;

failed_cleanup:
    kfree(buffer);
    return -E_INVAL;
}

/* sysfile_open - open file */
int
sysfile_open(const char *__path, uint32_t open_flags) {
    int ret;
    char *path;
    if ((ret = copy_path(&path, __path)) != 0) {
        return ret;
    }
    ret = file_open(path, open_flags);
    kfree(path);
    return ret;
}

/* sysfile_close - close file */
int
sysfile_close(int fd) {
    return file_close(fd);
}

/* sysfile_read - read file */
int
sysfile_read(int fd, void *base, size_t len) {
    struct mm_struct *mm = current->mm;
    if (len == 0) {
        return 0;
    }
    if (!file_testfd(fd, 1, 0)) {
        return -E_INVAL;
    }
    void *buffer;
    if ((buffer = kmalloc(IOBUF_SIZE)) == NULL) {
        return -E_NO_MEM;
    }

    int ret = 0;
    size_t copied = 0, alen;
    while (len != 0) {
        size_t requested = len < IOBUF_SIZE ? len : IOBUF_SIZE;
        alen = requested;
        if (alen > len) {
            alen = len;
        }
        ret = file_read(fd, buffer, alen, &alen);
        if (alen != 0) {
            lock_mm(mm);
            {
                if (copy_to_user(mm, base, buffer, alen)) {
                    assert(len >= alen);
                    base += alen, len -= alen, copied += alen;
                }
                else if (ret == 0) {
                    ret = -E_INVAL;
                }
            }
            unlock_mm(mm);
        }
        /* A pipe is a byte stream: a successful short read is a complete
         * read operation and must be returned immediately instead of trying
         * to fill the caller's entire buffer.  This also matches regular
         * file EOF behavior and avoids a second blocking read. */
        if (ret != 0 || alen == 0 || alen < requested) {
            goto out;
        }
    }

out:
    kfree(buffer);
    if (copied != 0) {
        return copied;
    }
    return ret;
}

/* sysfile_write - write file */
int
sysfile_write(int fd, void *base, size_t len) {
    struct mm_struct *mm = current->mm;
    if (len == 0) {
        return 0;
    }
    if (!file_testfd(fd, 0, 1)) {
        return -E_INVAL;
    }
    void *buffer;
    if ((buffer = kmalloc(IOBUF_SIZE)) == NULL) {
        return -E_NO_MEM;
    }

    int ret = 0;
    size_t copied = 0, alen;
    while (len != 0) {
        if ((alen = IOBUF_SIZE) > len) {
            alen = len;
        }
        lock_mm(mm);
        {
            if (!copy_from_user(mm, buffer, base, alen, 0)) {
                ret = -E_INVAL;
            }
        }
        unlock_mm(mm);
        if (ret == 0) {
            ret = file_write(fd, buffer, alen, &alen);
            if (alen != 0) {
                assert(len >= alen);
                base += alen, len -= alen, copied += alen;
            }
        }
        if (ret != 0 || alen == 0) {
            goto out;
        }
    }

out:
    kfree(buffer);
    if (copied != 0) {
        return copied;
    }
    return ret;
}

/* sysfile_seek - seek file */
int
sysfile_seek(int fd, off_t pos, int whence) {
    return file_seek(fd, pos, whence);
}

/* sysfile_fstat - stat file */
int
sysfile_fstat(int fd, struct stat *__stat) {
    struct mm_struct *mm = current->mm;
    int ret;
    struct stat __local_stat, *stat = &__local_stat;
    if ((ret = file_fstat(fd, stat)) != 0) {
        return ret;
    }

    lock_mm(mm);
    {
        if (!copy_to_user(mm, __stat, stat, sizeof(struct stat))) {
            ret = -E_INVAL;
        }
    }
    unlock_mm(mm);
    return ret;
}

int
sysfile_stat(const char *__path, struct stat *__stat, bool nofollow) {
    struct mm_struct *mm = current->mm;
    struct inode *node;
    struct stat local_stat;
    char *path;
    int ret = 0;

    /* SFS currently has no symlink traversal operation.  Keep the explicit
     * nofollow argument in the internal ABI so lstat can share validation and
     * gain distinct semantics when the filesystem grows that operation. */
    (void)nofollow;
    if (mm == NULL || __stat == NULL ||
        (ret = copy_path(&path, __path)) != 0) {
        return ret != 0 ? ret : -E_INVAL;
    }
    ret = vfs_lookup(path, &node);
    kfree(path);
    if (ret != 0) {
        return ret;
    }
    ret = vop_fstat(node, &local_stat);
    vop_ref_dec(node);
    if (ret != 0) {
        return ret;
    }
    lock_mm(mm);
    ret = copy_to_user(mm, __stat, &local_stat, sizeof(local_stat)) ?
          0 : -E_INVAL;
    unlock_mm(mm);
    return ret;
}

/* sysfile_fsync - sync file */
int
sysfile_fsync(int fd) {
    return file_fsync(fd);
}

int
sysfile_ftruncate(int fd, off_t length) {
    return file_ftruncate(fd, length);
}

int
sysfile_truncate(const char *__path, off_t length) {
    struct inode *node;
    char *path;
    int ret = 0;

    if (length < 0 || (ret = copy_path(&path, __path)) != 0) {
        return ret != 0 ? ret : -E_INVAL;
    }
    ret = vfs_lookup(path, &node);
    kfree(path);
    if (ret != 0) {
        return ret;
    }
    ret = vop_truncate(node, length);
    vop_ref_dec(node);
    return ret;
}

int
sysfile_pread(int fd, void *base, size_t len, off_t offset) {
    struct mm_struct *mm = current->mm;
    uint8_t *buffer;
    size_t total = 0;
    int ret = 0;

    if (mm == NULL || offset < 0) {
        return -E_INVAL;
    }
    if (len == 0) {
        return 0;
    }
    if ((buffer = kmalloc(IOBUF_SIZE)) == NULL) {
        return -E_NO_MEM;
    }
    while (total < len) {
        size_t requested = len - total;
        size_t copied = 0;
        off_t position;
        if (requested > IOBUF_SIZE) {
            requested = IOBUF_SIZE;
        }
        if (offset > OFF_T_MAX - (off_t)total) {
            ret = -E_INVAL;
            break;
        }
        position = offset + (off_t)total;
        ret = file_pread(fd, buffer, requested, position, &copied);
        if (copied != 0) {
            lock_mm(mm);
            if (!copy_to_user(mm, (uint8_t *)base + total, buffer, copied)) {
                ret = -E_INVAL;
            }
            unlock_mm(mm);
            if (ret == -E_INVAL) {
                break;
            }
            total += copied;
        }
        if (ret != 0 || copied < requested) {
            break;
        }
    }
    kfree(buffer);
    return total != 0 ? (int)total : ret;
}

int
sysfile_pwrite(int fd, const void *base, size_t len, off_t offset) {
    struct mm_struct *mm = current->mm;
    uint8_t *buffer;
    size_t total = 0;
    int ret = 0;

    if (mm == NULL || offset < 0) {
        return -E_INVAL;
    }
    if (len == 0) {
        return 0;
    }
    if ((buffer = kmalloc(IOBUF_SIZE)) == NULL) {
        return -E_NO_MEM;
    }
    while (total < len) {
        size_t requested = len - total;
        size_t copied = 0;
        off_t position;
        if (requested > IOBUF_SIZE) {
            requested = IOBUF_SIZE;
        }
        if (offset > OFF_T_MAX - (off_t)total) {
            ret = -E_INVAL;
            break;
        }
        position = offset + (off_t)total;
        lock_mm(mm);
        if (!copy_from_user(mm, buffer, (const uint8_t *)base + total,
                            requested, 0)) {
            ret = -E_INVAL;
        }
        unlock_mm(mm);
        if (ret != 0) {
            break;
        }
        ret = file_pwrite(fd, buffer, requested, position, &copied);
        total += copied;
        if (ret != 0 || copied < requested) {
            break;
        }
    }
    kfree(buffer);
    return total != 0 ? (int)total : ret;
}

static int
sysfile_copy_iov(struct mm_struct *mm, struct iovec *local,
                 const struct iovec *user_iov, size_t count, bool write) {
    size_t i;
    if (mm == NULL || local == NULL || count > FS_IOV_MAX ||
        (count != 0 && user_iov == NULL)) {
        return -E_INVAL;
    }
    if (count == 0) {
        return 0;
    }
    lock_mm(mm);
    if (!copy_from_user(mm, local, user_iov,
                        count * sizeof(*local), 0)) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    for (i = 0; i < count; i++) {
        if (local[i].iov_len != 0 &&
            !user_mem_check(mm, (uintptr_t)local[i].iov_base,
                            local[i].iov_len, write)) {
            unlock_mm(mm);
            return -E_INVAL;
        }
    }
    unlock_mm(mm);
    return 0;
}

int
sysfile_readv(int fd, const struct iovec *user_iov, size_t count) {
    struct mm_struct *mm = current->mm;
    struct iovec local[FS_IOV_MAX];
    size_t i, total = 0;
    int ret;

    ret = sysfile_copy_iov(mm, local, user_iov, count, 1);
    if (ret != 0) {
        return ret;
    }
    for (i = 0; i < count; i++) {
        if (local[i].iov_len == 0) {
            continue;
        }
        ret = sysfile_read(fd, local[i].iov_base, local[i].iov_len);
        if (ret > 0) {
            total += (size_t)ret;
        }
        if (ret < 0 || (size_t)ret < local[i].iov_len) {
            break;
        }
    }
    return total != 0 ? (int)total : ret;
}

int
sysfile_writev(int fd, const struct iovec *user_iov, size_t count) {
    struct mm_struct *mm = current->mm;
    struct iovec local[FS_IOV_MAX];
    size_t i, total = 0;
    int ret;

    ret = sysfile_copy_iov(mm, local, user_iov, count, 0);
    if (ret != 0) {
        return ret;
    }
    for (i = 0; i < count; i++) {
        if (local[i].iov_len == 0) {
            continue;
        }
        ret = sysfile_write(fd, local[i].iov_base, local[i].iov_len);
        if (ret > 0) {
            total += (size_t)ret;
        }
        if (ret < 0 || (size_t)ret < local[i].iov_len) {
            break;
        }
    }
    return total != 0 ? (int)total : ret;
}

/* sysfile_chdir - change dir */
int
sysfile_chdir(const char *__path) {
    int ret;
    char *path;
    if ((ret = copy_path(&path, __path)) != 0) {
        return ret;
    }
    ret = vfs_chdir(path);
    kfree(path);
    return ret;
}

int
sysfile_fchdir(int fd) {
    return file_chdir(fd);
}

/* sysfile_mkdir - create a directory */
int
sysfile_mkdir(const char *__path) {
    int ret;
    char *path;
    if ((ret = copy_path(&path, __path)) != 0) {
        return ret;
    }
    ret = vfs_mkdir(path);
    kfree(path);
    return ret;
}

/* sysfile_link - link file */
int
sysfile_link(const char *__path1, const char *__path2) {
    int ret;
    char *old_path, *new_path;
    if ((ret = copy_path(&old_path, __path1)) != 0) {
        return ret;
    }
    if ((ret = copy_path(&new_path, __path2)) != 0) {
        kfree(old_path);
        return ret;
    }
    ret = vfs_link(old_path, new_path);
    kfree(old_path), kfree(new_path);
    return ret;
}

/* sysfile_rename - rename file */
int
sysfile_rename(const char *__path1, const char *__path2) {
    int ret;
    char *old_path, *new_path;
    if ((ret = copy_path(&old_path, __path1)) != 0) {
        return ret;
    }
    if ((ret = copy_path(&new_path, __path2)) != 0) {
        kfree(old_path);
        return ret;
    }
    ret = vfs_rename(old_path, new_path);
    kfree(old_path), kfree(new_path);
    return ret;
}

/* sysfile_unlink - unlink file */
int
sysfile_unlink(const char *__path) {
    int ret;
    char *path;
    if ((ret = copy_path(&path, __path)) != 0) {
        return ret;
    }
    ret = vfs_unlink(path);
    kfree(path);
    return ret;
}

int
sysfile_rmdir(const char *__path) {
    int ret;
    char *path;
    if ((ret = copy_path(&path, __path)) != 0) return ret;
    ret = vfs_rmdir(path);
    kfree(path);
    return ret;
}

/* sysfile_get cwd - get current working directory */
int
sysfile_getcwd(char *buf, size_t len) {
    struct mm_struct *mm = current->mm;
    if (len == 0) {
        return -E_INVAL;
    }

    int ret = -E_INVAL;
    lock_mm(mm);
    {
        if (user_mem_check(mm, (uintptr_t)buf, len, 1)) {
            struct iobuf __iob, *iob = iobuf_init(&__iob, buf, len, 0);
            ret = vfs_getcwd(iob);
        }
    }
    unlock_mm(mm);
    return ret;
}

/* sysfile_getdirentry - get the file entry in DIR */
int
sysfile_getdirentry(int fd, struct dirent *__direntp) {
    struct mm_struct *mm = current->mm;
    struct dirent *direntp;
    if ((direntp = kmalloc(sizeof(struct dirent))) == NULL) {
        return -E_NO_MEM;
    }

    int ret = 0;
    lock_mm(mm);
    {
        if (!copy_from_user(mm, &(direntp->offset), &(__direntp->offset), sizeof(direntp->offset), 1)) {
            ret = -E_INVAL;
        }
    }
    unlock_mm(mm);

    if (ret != 0 || (ret = file_getdirentry(fd, direntp)) != 0) {
        goto out;
    }

    lock_mm(mm);
    {
        if (!copy_to_user(mm, __direntp, direntp, sizeof(struct dirent))) {
            ret = -E_INVAL;
        }
    }
    unlock_mm(mm);

out:
    kfree(direntp);
    return ret;
}

/* sysfile_dup -  duplicate fd1 to fd2 */
int
sysfile_dup(int fd1, int fd2) {
    return file_dup(fd1, fd2);
}

int
sysfile_pipe(int *fd_store) {
    struct mm_struct *mm = current->mm;
    int fds[2];
    int ret;

    if (mm == NULL || fd_store == NULL) {
        return -E_INVAL;
    }
    ret = file_pipe(fds);
    if (ret != 0) {
        return ret;
    }
    lock_mm(mm);
    if (!copy_to_user(mm, fd_store, fds, sizeof(fds))) {
        ret = -E_INVAL;
    }
    unlock_mm(mm);
    if (ret != 0) {
        file_close(fds[0]);
        file_close(fds[1]);
    }
    return ret;
}

int
sysfile_pipe2(int *fd_store, uint32_t flags) {
    /* O_NONBLOCK is introduced with fcntl; reject unknown pipe flags rather
     * than silently changing blocking semantics. */
    if (flags != 0) {
        return -E_INVAL;
    }
    return sysfile_pipe(fd_store);
}

int
sysfile_socketpair(int domain, int type, int protocol, int *fd_store) {
    struct mm_struct *mm = current->mm;
    int fds[2];
    int ret;

    if (mm == NULL || fd_store == NULL) {
        return -E_INVAL;
    }
    ret = file_socketpair_create(domain, type, protocol, fds);
    if (ret != 0) {
        return ret;
    }
    lock_mm(mm);
    if (!copy_to_user(mm, fd_store, fds, sizeof(fds))) {
        ret = -E_INVAL;
    }
    unlock_mm(mm);
    if (ret != 0) {
        file_close(fds[0]);
        file_close(fds[1]);
    }
    return ret;
}

int
sysfile_symlink(const char *target, const char *link_path) {
    char *target_copy = NULL, *link_copy = NULL;
    int ret;
    if ((ret = copy_path(&target_copy, target)) != 0 ||
        (ret = copy_path(&link_copy, link_path)) != 0) {
        kfree(target_copy);
        return ret;
    }
    ret = vfs_symlink(target_copy, link_copy);
    kfree(target_copy);
    kfree(link_copy);
    return ret;
}

int
sysfile_readlink(const char *path, char *buffer, size_t len) {
    struct mm_struct *mm = current->mm;
    struct iobuf iob;
    char *path_copy, *data;
    size_t used;
    int ret;

    if (mm == NULL || buffer == NULL || len == 0 ||
        (ret = copy_path(&path_copy, path)) != 0) {
        return ret != 0 ? ret : -E_INVAL;
    }
    data = kmalloc(len);
    if (data == NULL) {
        kfree(path_copy);
        return -E_NO_MEM;
    }
    iobuf_init(&iob, data, len, 0);
    ret = vfs_readlink(path_copy, &iob);
    used = iobuf_used(&iob);
    if (ret == 0) {
        lock_mm(mm);
        if (!copy_to_user(mm, buffer, data, used)) {
            ret = -E_INVAL;
        }
        unlock_mm(mm);
    }
    kfree(data);
    kfree(path_copy);
    return ret == 0 ? (int)used : ret;
}

int
sysfile_mkfifo(const char *__name, uint32_t open_flags) {
    return -E_UNIMP;
}
