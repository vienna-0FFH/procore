#ifndef __KERN_FS_CONFIG_H__
#define __KERN_FS_CONFIG_H__

/* Filesystem policy knobs.  Override with FS_DEFS+=-DNAME=value. */
#ifndef FS_PIPE_CAPACITY
#define FS_PIPE_CAPACITY             4096U
#endif

#if FS_PIPE_CAPACITY < 2
#error "FS_PIPE_CAPACITY must hold at least two bytes"
#endif

#endif /* !__KERN_FS_CONFIG_H__ */
