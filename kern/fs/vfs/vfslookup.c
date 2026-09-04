#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <error.h>
#include <assert.h>

/*
 * get_device- Common code to pull the device name, if any, off the front of a
 *             path and choose the inode to begin the name lookup relative to.
 */

static int
get_device(char *path, char **subpath, struct inode **node_store) {
    int i, slash = -1, colon = -1;
    for (i = 0; path[i] != '\0'; i ++) {
        if (path[i] == ':') { colon = i; break; }
        if (path[i] == '/') { slash = i; break; }
    }
    if (colon < 0 && slash != 0) {
        /* *
         * No colon before a slash, so no device name specified, and the slash isn't leading
         * or is also absent, so this is a relative path or just a bare filename. Start from
         * the current directory, and use the whole thing as the subpath.
         * */
        *subpath = path;
        return vfs_get_curdir(node_store);
    }
    if (colon > 0) {
        /* device:path - get root of device's filesystem */
        int device_end = colon;
        path[device_end] = '\0';

        /* device:/path - skip slash, treat as device:path */
        int subpath_start = device_end + 1;
        while (path[subpath_start] == '/') {
            subpath_start++;
        }
        *subpath = path + subpath_start;
        int ret = vfs_get_root(path, node_store);
        /* Callers may need to parse the same buffer again (for example,
         * vfs_open retries with vfs_lookup_parent after O_CREAT). */
        path[device_end] = ':';
        return ret;
    }

    /* *
     * we have either /path or :path
     * /path is a path relative to the root of the "boot filesystem"
     * :path is a path relative to the root of the current filesystem
     * */
    int ret;
    if (*path == '/') {
        if ((ret = vfs_get_bootfs(node_store)) != 0) {
            return ret;
        }
    }
    else {
        assert(*path == ':');
        struct inode *node;
        if ((ret = vfs_get_curdir(&node)) != 0) {
            return ret;
        }
        /* The current directory may not be a device, so it must have a fs. */
        assert(node->in_fs != NULL);
        *node_store = fsop_get_root(node->in_fs);
        vop_ref_dec(node);
    }

    /* ///... or :/... */
    while (*(++ path) == '/');
    *subpath = path;
    return 0;
}

/*
 * vfs_lookup - get the inode according to the path filename
 */
int
vfs_lookup(char *path, struct inode **node_store) {
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0) {
        return ret;
    }
    while (*path != '\0') {
        char *component;
        char *separator;
        struct inode *next;

        /* Repeated separators do not name an additional component. */
        while (*path == '/') {
            path++;
        }
        if (*path == '\0') {
            break;
        }

        component = path;
        while (*path != '\0' && *path != '/') {
            path++;
        }
        if ((size_t)(path - component) > FS_MAX_FNAME_LEN) {
            vop_ref_dec(node);
            return -E_TOO_BIG;
        }
        separator = (*path != '\0') ? path : NULL;
        if (separator != NULL) {
            *path++ = '\0';
        }

        ret = vop_lookup(node, component, &next);
        if (separator != NULL) {
            *separator = '/';
        }
        vop_ref_dec(node);
        if (ret != 0) {
            return ret;
        }
        node = next;
    }
    *node_store = node;
    return 0;
}

/*
 * vfs_lookup_parent - Name-to-vnode translation.
 *  (In BSD, both of these are subsumed by namei().)
 */
int
vfs_lookup_parent(char *path, struct inode **node_store, char **endp){
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0) {
        return ret;
    }

    /* Locate the final component while resolving every preceding component.
     * The caller owns the returned parent reference and may mutate the final
     * component in-place, which is why PATH is deliberately non-const. */
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        vop_ref_dec(node);
        return -E_INVAL;
    }

    for (;;) {
        char *component = path;
        char *slash = component;
        struct inode *next;

        while (*slash != '\0' && *slash != '/') {
            slash++;
        }
        if (*slash == '\0') {
            if ((size_t)(slash - component) > FS_MAX_FNAME_LEN) {
                vop_ref_dec(node);
                return -E_TOO_BIG;
            }
            *endp = component;
            *node_store = node;
            return 0;
        }

        /* Skip all separators after this component.  If they lead to the
         * end, the component is still the basename ("file/" semantics). */
        char *next_component = slash;
        while (*next_component == '/') {
            next_component++;
        }
        if ((size_t)(slash - component) > FS_MAX_FNAME_LEN) {
            vop_ref_dec(node);
            return -E_TOO_BIG;
        }
        *slash = '\0';
        if (*next_component == '\0') {
            *endp = component;
            *node_store = node;
            return 0;
        }

        ret = vop_lookup(node, component, &next);
        vop_ref_dec(node);
        if (ret != 0) {
            return ret;
        }
        node = next;
        path = next_component;
    }
}
