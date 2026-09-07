#include <defs.h>
#include <kmalloc.h>
#include <sem.h>
#include <vfs.h>
#include <dev.h>
#include <file.h>
#include <sfs.h>
#include <inode.h>
#include <assert.h>

void
fs_init(void) {
    vfs_init();
    dev_init();
    sfs_init();
}

void
fs_cleanup(void) {
    vfs_cleanup();
}

void
lock_files(struct files_struct *filesp) {
    down(&filesp->files_sem);
}

void
unlock_files(struct files_struct *filesp) {
    up(&filesp->files_sem);
}

struct files_struct *
files_create(void) {
    struct files_struct *filesp;
    static_assert((int)FILES_STRUCT_NENTRY > 128);

    filesp = kmalloc(sizeof(*filesp) + FILES_STRUCT_BUFSIZE);
    if (filesp != NULL) {
        filesp->pwd = NULL;
        filesp->fd_array = (void *)(filesp + 1);
        filesp->files_count = 0;
        sem_init(&filesp->files_sem, 1);
        fd_array_init(filesp->fd_array);
    }
    return filesp;
}

static int
files_detach(struct files_struct *filesp, int first,
             struct open_file **descriptions) {
    struct file *file;
    int fd, count = 0;

    lock_files(filesp);
    for (fd = first; fd < FILES_STRUCT_NENTRY; fd ++) {
        file = &filesp->fd_array[fd];
        if (file->status == FD_OPENED) {
            descriptions[count++] = fd_array_close(file);
        }
    }
    unlock_files(filesp);
    return count;
}

static void
files_put_descriptions(struct open_file **descriptions, int count) {
    int i;
    for (i = 0; i < count; i ++) {
        open_file_put(descriptions[i]);
    }
}

void
files_destroy(struct files_struct *filesp) {
    struct open_file *descriptions[FILES_STRUCT_NENTRY];
    struct inode *pwd;
    int count, fd;

    assert(filesp != NULL && files_count(filesp) == 0);
    lock_files(filesp);
    pwd = filesp->pwd;
    filesp->pwd = NULL;
    unlock_files(filesp);

    count = files_detach(filesp, 0, descriptions);
    for (fd = 0; fd < FILES_STRUCT_NENTRY; fd ++) {
        assert(filesp->fd_array[fd].status == FD_NONE);
    }
    files_put_descriptions(descriptions, count);
    if (pwd != NULL) {
        vop_ref_dec(pwd);
    }
    kfree(filesp);
}

void
files_closeall(struct files_struct *filesp) {
    struct open_file *descriptions[FILES_STRUCT_NENTRY];
    int count;

    assert(filesp != NULL && files_count(filesp) > 0);
    /* Keep stdin/stdout across exec, matching the existing process ABI. */
    count = files_detach(filesp, 2, descriptions);
    files_put_descriptions(descriptions, count);
}

int
dup_fs(struct files_struct *to, struct files_struct *from) {
    int fd;

    assert(to != NULL && from != NULL && to != from);
    assert(files_count(to) == 0 && files_count(from) > 0);

    /* The source lock gives fork one coherent descriptor/cwd snapshot. */
    lock_files(from);
    if ((to->pwd = from->pwd) != NULL) {
        vop_ref_inc(to->pwd);
    }
    for (fd = 0; fd < FILES_STRUCT_NENTRY; fd ++) {
        if (from->fd_array[fd].status == FD_OPENED) {
            fd_array_dup(&to->fd_array[fd], &from->fd_array[fd]);
        }
    }
    unlock_files(from);
    return 0;
}
