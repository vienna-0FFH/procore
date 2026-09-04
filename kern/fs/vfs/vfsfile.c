#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <unistd.h>
#include <stat.h>
#include <error.h>
#include <assert.h>

/* The current VFS has one namespace-capable filesystem (SFS).  Keep the
 * dispatch in this layer so adding another filesystem later does not leak
 * SFS details into pathname or syscall code. */
static int
vfs_require_dir(struct inode *node) {
    uint32_t type;
    int ret = vop_gettype(node, &type);
    if (ret != 0) {
        return ret;
    }
    return S_ISDIR(type) ? 0 : -E_NOTDIR;
}

enum vfs_sfs_dirop {
    VFS_SFS_MKDIR,
    VFS_SFS_UNLINK,
    VFS_SFS_LINK,
    VFS_SFS_RENAME,
};

static int
vfs_sfs_dirop(struct inode *dir, enum vfs_sfs_dirop op, const char *name,
              struct inode *target, struct inode *new_dir,
              const char *new_name) {
    if (!check_inode_type(dir, sfs_inode)) {
        return -E_UNIMP;
    }
    switch (op) {
    case VFS_SFS_MKDIR:
        return sfs_mkdir(dir, name);
    case VFS_SFS_UNLINK:
        return sfs_unlink(dir, name);
    case VFS_SFS_LINK:
        return sfs_link(dir, name, target);
    case VFS_SFS_RENAME:
        if (!check_inode_type(new_dir, sfs_inode)) {
            return -E_XDEV;
        }
        return sfs_rename(dir, name, new_dir, new_name);
    default:
        return -E_UNIMP;
    }
}


// open file in vfs, get/create inode for file with filename path.
int
vfs_open(char *path, uint32_t open_flags, struct inode **node_store) {
    bool can_write = 0;
    switch (open_flags & O_ACCMODE) {
    case O_RDONLY:
        break;
    case O_WRONLY:
    case O_RDWR:
        can_write = 1;
        break;
    default:
        return -E_INVAL;
    }

    if (open_flags & O_TRUNC) {
        if (!can_write) {
            return -E_INVAL;
        }
    }

    int ret; 
    struct inode *node;
    bool excl = (open_flags & O_EXCL) != 0;
    bool create = (open_flags & O_CREAT) != 0;
    ret = vfs_lookup(path, &node);

    if (ret != 0) {
        if (ret == -E_NOENT && (create)) {
            char *name;
            struct inode *dir;
            if ((ret = vfs_lookup_parent(path, &dir, &name)) != 0) {
                return ret;
            }
            if ((ret = vfs_require_dir(dir)) == 0) {
                ret = vop_create(dir, name, excl, &node);
            }
            vop_ref_dec(dir);
            if (ret != 0) {
                return ret;
            }
        } else return ret;
    } else if (excl && create) {
        vop_ref_dec(node);
        return -E_EXISTS;
    }
    assert(node != NULL);
    
    if ((ret = vop_open(node, open_flags)) != 0) {
        vop_ref_dec(node);
        return ret;
    }

    vop_open_inc(node);
    if (open_flags & O_TRUNC) {
        if ((ret = vop_truncate(node, 0)) != 0) {
            vop_open_dec(node);
            vop_ref_dec(node);
            return ret;
        }
    }
    *node_store = node;
    return 0;
}

// close file in vfs
int
vfs_close(struct inode *node) {
    vop_open_dec(node);
    vop_ref_dec(node);
    return 0;
}

// unimplement
int
vfs_unlink(char *path) {
    int ret;
    char *name;
    struct inode *dir;
    if ((ret = vfs_lookup_parent(path, &dir, &name)) != 0) {
        return ret;
    }
    if (*name == '\0' || strchr(name, '/') != NULL) {
        ret = -E_INVAL;
    }
    else if ((ret = vfs_require_dir(dir)) == 0) {
        ret = vfs_sfs_dirop(dir, VFS_SFS_UNLINK, name, NULL, NULL, NULL);
    }
    vop_ref_dec(dir);
    return ret;
}

// unimplement
int
vfs_rename(char *old_path, char *new_path) {
    int ret;
    char *old_name, *new_name;
    struct inode *old_dir, *new_dir;
    if ((ret = vfs_lookup_parent(old_path, &old_dir, &old_name)) != 0) {
        return ret;
    }
    if ((ret = vfs_lookup_parent(new_path, &new_dir, &new_name)) != 0) {
        vop_ref_dec(old_dir);
        return ret;
    }
    if (*old_name == '\0' || *new_name == '\0' ||
        strchr(old_name, '/') != NULL || strchr(new_name, '/') != NULL) {
        ret = -E_INVAL;
    }
    else if ((ret = vfs_require_dir(old_dir)) == 0 &&
             (ret = vfs_require_dir(new_dir)) == 0) {
        if (vop_fs(old_dir) != vop_fs(new_dir)) {
            ret = -E_XDEV;
        }
        else {
            ret = vfs_sfs_dirop(old_dir, VFS_SFS_RENAME,
                                old_name, NULL, new_dir, new_name);
        }
    }
    vop_ref_dec(old_dir);
    vop_ref_dec(new_dir);
    return ret;
}

// unimplement
int
vfs_link(char *old_path, char *new_path) {
    int ret;
    char *new_name;
    struct inode *target, *new_dir;
    if ((ret = vfs_lookup(old_path, &target)) != 0) {
        return ret;
    }
    if ((ret = vfs_lookup_parent(new_path, &new_dir, &new_name)) != 0) {
        vop_ref_dec(target);
        return ret;
    }
    if (*new_name == '\0' || strchr(new_name, '/') != NULL) {
        ret = -E_INVAL;
    }
    else if (vop_fs(target) != vop_fs(new_dir)) {
        ret = -E_XDEV;
    }
    else if ((ret = vfs_require_dir(new_dir)) == 0) {
        ret = vfs_sfs_dirop(new_dir, VFS_SFS_LINK, new_name, target, NULL, NULL);
    }
    vop_ref_dec(new_dir);
    vop_ref_dec(target);
    return ret;
}

// unimplement
int
vfs_symlink(char *old_path, char *new_path) {
    return -E_UNIMP;
}

// unimplement
int
vfs_readlink(char *path, struct iobuf *iob) {
    return -E_UNIMP;
}

// unimplement
int
vfs_mkdir(char *path){
    int ret;
    char *name;
    struct inode *dir;
    if ((ret = vfs_lookup_parent(path, &dir, &name)) != 0) {
        return ret;
    }
    if (*name == '\0' || strchr(name, '/') != NULL) {
        ret = -E_INVAL;
    }
    else if ((ret = vfs_require_dir(dir)) == 0) {
        ret = vfs_sfs_dirop(dir, VFS_SFS_MKDIR, name, NULL, NULL, NULL);
    }
    vop_ref_dec(dir);
    return ret;
}
