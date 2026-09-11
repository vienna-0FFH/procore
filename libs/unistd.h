#ifndef __LIBS_UNISTD_H__
#define __LIBS_UNISTD_H__

#include <defs.h>

/* Keep the user ABI independent of the kernel include path. */
#ifndef NET_LOCAL_IP
#define NET_LOCAL_IP        0x0F02000AU
#endif
#ifndef NET_GATEWAY_IP
#define NET_GATEWAY_IP      0x0202000AU
#endif
#ifndef NET_DNS_IP
#define NET_DNS_IP          0x0302000AU
#endif

#define T_SYSCALL           0x80

/* syscall number */
#define SYS_exit            1
#define SYS_fork            2
#define SYS_wait            3
#define SYS_exec            4
#define SYS_clone           5
#define SYS_yield           10
#define SYS_sleep           11
#define SYS_kill            12
#define SYS_gettime         17
#define SYS_getpid          18
#define SYS_getppid         19
#define SYS_mmap            20
#define SYS_munmap          21
#define SYS_shmem           22
#define SYS_gettid          23
#define SYS_getcpu          24
#define SYS_brk             25
#define SYS_setaffinity     26
#define SYS_getaffinity     27
#define SYS_getcpustat      28
#define SYS_socket          140
#define SYS_bind            141
#define SYS_sendto          142
#define SYS_recvfrom        143
#define SYS_netstat         144
#define SYS_connect         145
#define SYS_send            146
#define SYS_recv            147
#define SYS_getsockname     148
#define SYS_getpeername     149
#define SYS_listen          150
#define SYS_accept          151
#define SYS_shutdown        152
#define SYS_raise           153
#define SYS_sigaction       154
#define SYS_sigprocmask     155
#define SYS_sigreturn       156
#define SYS_stat            157
#define SYS_lstat           158
#define SYS_ftruncate       159
#define SYS_truncate        160
#define SYS_pread           161
#define SYS_pwrite          162
#define SYS_readv           163
#define SYS_writev          164
#define SYS_clock_gettime   165
#define SYS_clock_getres    166
#define SYS_gettimeofday    167
#define SYS_nanosleep       168
#define SYS_time            169
#define SYS_uname           170
#define SYS_sysinfo         171
#define SYS_getuid          172
#define SYS_geteuid         173
#define SYS_getgid          174
#define SYS_getegid         175
#define SYS_getresuid       176
#define SYS_getresgid       177
#define SYS_wait4           178
#define SYS_fchdir          179
#define SYS_rmdir           180
#define SYS_getsockopt      181
#define SYS_setsockopt      182
#define SYS_putc            30
#define SYS_pgdir           31
#define SYS_open            100
#define SYS_close           101
#define SYS_read            102
#define SYS_write           103
#define SYS_seek            104
#define SYS_fstat           110
#define SYS_fsync           111
#define SYS_chdir           120
#define SYS_getcwd          121
#define SYS_getdirentry     128
#define SYS_dup             130
#define SYS_mkdir           131
#define SYS_link            132
#define SYS_unlink          133
#define SYS_rename          134
#define SYS_pipe            135
#define SYS_pipe2           136
#define SYS_fcntl           137
#define SYS_poll            138
/* OLNY FOR core */
#define SYS_lab6_set_priority 255

/* SYS_fork flags */
#define CLONE_VM            0x00000100  // set if VM shared between processes
#define CLONE_THREAD        0x00000200  // thread group
#define CLONE_FS            0x00000800  // set if shared between processes

#define WNOHANG             0x00000001

/* Anonymous mappings; file-backed mappings require a page-cache contract. */
#define PROT_NONE           0x0
#define PROT_READ           0x1
#define PROT_WRITE          0x2
#define PROT_EXEC           0x4
#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_ANON            MAP_ANONYMOUS
#define MAP_FAILED          ((void *)(uintptr_t)-1)
/* User-visible page granularity; the kernel's MMU header remains canonical. */
#define UCORE_PAGE_SIZE     4096

/* Snapshot returned by getcpustat().  Counters are monotonic modulo 32 bits;
 * callers that need long-running totals can account for wraparound. */
struct cpu_stat {
    uint32_t cpu_id;
    uint32_t online;
    uint32_t ticks;
    uint32_t switches;
    uint32_t idle_ticks;
    uint32_t migrations;
    uint32_t runnable;
};

#ifndef __UCORE_UTSNAME_DEFINED
#define __UCORE_UTSNAME_DEFINED
#define UCORE_UTS_FIELD_LEN 65
struct utsname {
    char sysname[UCORE_UTS_FIELD_LEN];
    char nodename[UCORE_UTS_FIELD_LEN];
    char release[UCORE_UTS_FIELD_LEN];
    char version[UCORE_UTS_FIELD_LEN];
    char machine[UCORE_UTS_FIELD_LEN];
    char domainname[UCORE_UTS_FIELD_LEN];
};
#endif

#ifndef __UCORE_SYSINFO_DEFINED
#define __UCORE_SYSINFO_DEFINED
struct sysinfo {
    int32_t uptime;
    uint32_t loads[3];
    uint32_t totalram;
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
    char _f[8];
};
#endif

/* VFS flags */
// flags for open: choose one of these
#define O_RDONLY            0           // open for reading only
#define O_WRONLY            1           // open for writing only
#define O_RDWR              2           // open for reading and writing
// then or in any of these:
#define O_CREAT             0x00000004  // create file if it does not exist
#define O_EXCL              0x00000008  // error if O_CREAT and the file exists
#define O_TRUNC             0x00000010  // truncate file upon open
#define O_APPEND            0x00000020  // append on each write
#define O_NONBLOCK          0x00004000  // do not wait for pipe/socket readiness

/* Socket half-close directions. */
#define SHUT_RD             0
#define SHUT_WR             1
#define SHUT_RDWR           2
// additonal related definition
#define O_ACCMODE           3           // mask for O_RDONLY / O_WRONLY / O_RDWR

/* fcntl commands and descriptor flags. */
#define F_DUPFD             0
#define F_GETFD             1
#define F_SETFD             2
#define F_GETFL             3
#define F_SETFL             4
#define FD_CLOEXEC          0x00000001

/* poll event bits. */
#define POLLIN              0x0001
#define POLLOUT             0x0004
#define POLLERR             0x0008
#define POLLHUP             0x0010
#define POLLNVAL            0x0020

struct pollfd {
    int fd;
    int16_t events;
    int16_t revents;
};

struct iovec {
    void *iov_base;
    size_t iov_len;
};

#define CLOCK_REALTIME             0
#define CLOCK_MONOTONIC            1
#define CLOCK_PROCESS_CPUTIME_ID   2
#define CLOCK_THREAD_CPUTIME_ID    3
#define CLOCK_REALTIME_COARSE      5
#define CLOCK_MONOTONIC_COARSE     6
#define CLOCK_MONOTONIC_RAW        4
#define CLOCK_BOOTTIME             7

#ifndef __UCORE_TIMESPEC_DEFINED
#define __UCORE_TIMESPEC_DEFINED
struct timespec {
    int32_t tv_sec;
    int32_t tv_nsec;
};
#endif

#ifndef __UCORE_TIMEVAL_DEFINED
#define __UCORE_TIMEVAL_DEFINED
struct timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};
#endif

#ifndef __UCORE_TIMEZONE_DEFINED
#define __UCORE_TIMEZONE_DEFINED
struct timezone {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
};
#endif

#define NO_FD               -0x9527     // invalid fd

/* lseek codes */
#define LSEEK_SET           0           // seek relative to beginning of file
#define LSEEK_CUR           1           // seek relative to current position in file
#define LSEEK_END           2           // seek relative to end of file

#define FS_MAX_DNAME_LEN    31
#define FS_MAX_FNAME_LEN    255
#define FS_MAX_FPATH_LEN    4095

#define EXEC_MAX_ARG_NUM    32
#define EXEC_MAX_ARG_LEN    4095

/* Minimal IPv4 datagram ABI.  Ports use network byte order. */
#define AF_INET             2
#define SOCK_DGRAM          2
#define SOCK_STREAM         1
#define IPPROTO_UDP         17
#define IPPROTO_TCP         6
#define IPPROTO_IP          0
#define SOL_SOCKET          1
#define SO_DEBUG            1
#define SO_REUSEADDR        2
#define SO_TYPE             3
#define SO_ERROR            4
#define SO_DONTROUTE        5
#define SO_BROADCAST        6
#define SO_SNDBUF           7
#define SO_RCVBUF           8
#define SO_KEEPALIVE        9
#define SO_RCVLOWAT         18
#define SO_SNDLOWAT         19
#define IP_TTL              2
#define TCP_NODELAY         1
#define TCP_MAXSEG          2
#define INADDR_ANY          0U
#define INADDR_LOOPBACK     0x0100007FU

/* First-phase standard signal ABI. */
#define SIGHUP              1
#define SIGINT              2
#define SIGQUIT             3
#define SIGILL              4
#define SIGTRAP             5
#define SIGABRT             6
#define SIGFPE              8
#define SIGUSR1            10
#define SIGKILL             9
#define SIGSEGV            11
#define SIGPIPE            13
#define SIGALRM            14
#define SIGTERM            15
#define SIGCHLD            17
#define SIGCONT            18
#define SIGSTOP            19
#define SIGTSTP            20
#define UCORE_NSIG         32

#define SIG_DFL             0U
#define SIG_IGN             1U
#define SIG_BLOCK           0
#define SIG_UNBLOCK         1
#define SIG_SETMASK         2
#define SA_RESTART          0x00000001U

typedef uint32_t sigset_t;

struct sigaction {
    uintptr_t sa_handler;
    uintptr_t sa_restorer;
    uint32_t sa_flags;
    sigset_t sa_mask;
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
};

struct net_stats {
    uint32_t sockets;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t dropped_packets;
    /* Hardware counters are zero when no supported NIC is present. */
    uint32_t hw_devices;
    uint32_t hw_link_up;
    uint32_t hw_tx_packets;
    uint32_t hw_rx_packets;
    uint32_t hw_tx_errors;
    uint32_t hw_rx_errors;
};

static inline uint16_t
htons(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static inline uint16_t
ntohs(uint16_t value) {
    return htons(value);
}

#endif /* !__LIBS_UNISTD_H__ */
