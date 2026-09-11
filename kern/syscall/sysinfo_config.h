#ifndef __KERN_SYSCALL_SYSINFO_CONFIG_H__
#define __KERN_SYSCALL_SYSINFO_CONFIG_H__

/* OS identity and policy values exposed through the stable user ABI. */
#ifndef UCORE_SYSNAME
#define UCORE_SYSNAME                 "uCore"
#endif
#ifndef UCORE_NODENAME
#define UCORE_NODENAME                "ucore"
#endif
#ifndef UCORE_RELEASE
#define UCORE_RELEASE                 "0.1"
#endif
#ifndef UCORE_VERSION
#define UCORE_VERSION                 "uCore SMP"
#endif
#ifndef UCORE_MACHINE
#define UCORE_MACHINE                 "i386"
#endif
#ifndef UCORE_DOMAINNAME
#define UCORE_DOMAINNAME              "localdomain"
#endif
#ifndef UCORE_DEFAULT_UID
#define UCORE_DEFAULT_UID             0U
#endif
#ifndef UCORE_DEFAULT_GID
#define UCORE_DEFAULT_GID             0U
#endif

#endif /* !__KERN_SYSCALL_SYSINFO_CONFIG_H__ */
