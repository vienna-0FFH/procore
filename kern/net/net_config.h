#ifndef __KERN_NET_CONFIG_H__
#define __KERN_NET_CONFIG_H__

/* Network policy knobs.  Override with NET_DEFS+=-DNAME=value. */
#ifndef NET_MAX_SOCKETS
#define NET_MAX_SOCKETS              64
#endif

#ifndef NET_RX_QUEUE_LIMIT
#define NET_RX_QUEUE_LIMIT           16
#endif

#ifndef NET_MAX_DATAGRAM
#define NET_MAX_DATAGRAM             1472
#endif

#ifndef NET_EPHEMERAL_FIRST
#define NET_EPHEMERAL_FIRST          49152
#endif

#ifndef NET_EPHEMERAL_LAST
#define NET_EPHEMERAL_LAST           65535
#endif

#if NET_MAX_SOCKETS < 1
#error "NET_MAX_SOCKETS must be positive"
#endif
#if NET_RX_QUEUE_LIMIT < 1
#error "NET_RX_QUEUE_LIMIT must be positive"
#endif
#if NET_MAX_DATAGRAM < 1
#error "NET_MAX_DATAGRAM must be positive"
#endif
#if NET_EPHEMERAL_FIRST < 1024 || NET_EPHEMERAL_FIRST > NET_EPHEMERAL_LAST
#error "invalid ephemeral port range"
#endif

#endif /* !__KERN_NET_CONFIG_H__ */
