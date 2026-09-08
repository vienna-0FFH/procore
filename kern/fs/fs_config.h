#ifndef __KERN_FS_CONFIG_H__
#define __KERN_FS_CONFIG_H__

/* Filesystem policy knobs.  Override with FS_DEFS+=-DNAME=value. */
#ifndef FS_PIPE_CAPACITY
#define FS_PIPE_CAPACITY             4096U
#endif

#ifndef FS_POLL_MAX_FDS
#define FS_POLL_MAX_FDS              64U
#endif

#if FS_PIPE_CAPACITY < 2
#error "FS_PIPE_CAPACITY must hold at least two bytes"
#endif
#if FS_POLL_MAX_FDS < 1
#error "FS_POLL_MAX_FDS must be positive"
#endif

#endif /* !__KERN_FS_CONFIG_H__ */
