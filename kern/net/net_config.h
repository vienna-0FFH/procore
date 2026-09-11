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

#ifndef NET_MTU
#define NET_MTU                      1500U
#endif

#ifndef NET_ETH_MIN_FRAME
#define NET_ETH_MIN_FRAME             60U
#endif

#ifndef NET_EPHEMERAL_FIRST
#define NET_EPHEMERAL_FIRST          49152
#endif

#ifndef NET_EPHEMERAL_LAST
#define NET_EPHEMERAL_LAST           65535
#endif

/* Guest IPv4 policy.  Values use the same network-order representation as
 * sockaddr_in: 10.0.2.15 is 0x0F02000A on this little-endian target. */
#ifndef NET_LOCAL_IP
#define NET_LOCAL_IP                 0x0F02000AU
#endif

#ifndef NET_NETMASK
#define NET_NETMASK                  0x00FFFFFFU
#endif

#ifndef NET_GATEWAY_IP
#define NET_GATEWAY_IP               0x0202000AU
#endif

#ifndef NET_RX_POLL_BUDGET
#define NET_RX_POLL_BUDGET           8U
#endif

#ifndef NET_ARP_CACHE_SIZE
#define NET_ARP_CACHE_SIZE           8U
#endif

#ifndef NET_ARP_TTL_TICKS
#define NET_ARP_TTL_TICKS            6000U
#endif

#ifndef NET_ARP_REQUEST_RETRIES
#define NET_ARP_REQUEST_RETRIES       4U
#endif

#ifndef NET_ARP_WAIT_TICKS
#define NET_ARP_WAIT_TICKS             200U
#endif


#ifndef NET_IP_TTL
#define NET_IP_TTL                   64U
#endif

#ifndef NET_DEFAULT_SOCKET_BUFFER
#define NET_DEFAULT_SOCKET_BUFFER    16384U
#endif

#ifndef NET_TCP_MSS
#define NET_TCP_MSS                  1400U
#endif

#ifndef NET_TCP_CONNECT_TIMEOUT
#define NET_TCP_CONNECT_TIMEOUT      500U
#endif

#ifndef NET_TCP_RX_QUEUE_LIMIT
#define NET_TCP_RX_QUEUE_LIMIT       32U
#endif

#ifndef NET_TCP_RETRY_TICKS
#define NET_TCP_RETRY_TICKS           100U
#endif

#ifndef NET_TCP_RETRY_LIMIT
#define NET_TCP_RETRY_LIMIT           3U
#endif

#ifndef NET_TCP_INITIAL_CWND_SEGMENTS
#define NET_TCP_INITIAL_CWND_SEGMENTS 2U
#endif

#ifndef NET_TCP_INITIAL_SSTHRESH_SEGMENTS
#define NET_TCP_INITIAL_SSTHRESH_SEGMENTS 16U
#endif

#ifndef NET_TCP_MAX_WINDOW
#define NET_TCP_MAX_WINDOW            65535U
#endif

#ifndef NET_TCP_MAX_WRITE
#define NET_TCP_MAX_WRITE             262144U
#endif

#ifndef NET_TCP_LISTEN_BACKLOG
#define NET_TCP_LISTEN_BACKLOG        8U
#endif

#ifndef NET_TCP_SHUTDOWN_TIMEOUT
#define NET_TCP_SHUTDOWN_TIMEOUT      500U
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
#if NET_MTU < NET_MAX_DATAGRAM + 28
#error "NET_MTU must fit an IPv4/UDP datagram and headers"
#endif
#if NET_ETH_MIN_FRAME < 60 || NET_ETH_MIN_FRAME > NET_MTU
#error "NET_ETH_MIN_FRAME must fit the Ethernet minimum and MTU"
#endif
#if NET_EPHEMERAL_FIRST < 1024 || NET_EPHEMERAL_FIRST > NET_EPHEMERAL_LAST
#error "invalid ephemeral port range"
#endif
#if NET_RX_POLL_BUDGET < 1
#error "NET_RX_POLL_BUDGET must be positive"
#endif
#if NET_ARP_CACHE_SIZE < 1
#error "NET_ARP_CACHE_SIZE must be positive"
#endif
#if NET_ARP_REQUEST_RETRIES < 1 || NET_ARP_WAIT_TICKS < 1
#error "ARP resolution bounds must be positive"
#endif
#if NET_TCP_MSS < 64 || NET_TCP_MSS > NET_MAX_DATAGRAM
#error "NET_TCP_MSS must fit a datagram payload"
#endif
#if NET_TCP_CONNECT_TIMEOUT < 1
#error "NET_TCP_CONNECT_TIMEOUT must be positive"
#endif
#if NET_TCP_RX_QUEUE_LIMIT < 1
#error "NET_TCP_RX_QUEUE_LIMIT must be positive"
#endif
#if NET_TCP_RETRY_TICKS < 1 || NET_TCP_RETRY_LIMIT < 1
#error "TCP retry policy must be positive"
#endif
#if NET_TCP_INITIAL_CWND_SEGMENTS < 1 || \
    NET_TCP_INITIAL_SSTHRESH_SEGMENTS < NET_TCP_INITIAL_CWND_SEGMENTS
#error "invalid TCP congestion window policy"
#endif
#if NET_TCP_MAX_WINDOW < NET_TCP_MSS
#error "TCP maximum window must fit one MSS"
#endif
#if NET_TCP_MAX_WRITE < NET_TCP_MSS
#error "TCP maximum write must fit one MSS"
#endif
#if NET_TCP_LISTEN_BACKLOG < 1
#error "TCP listen backlog must be positive"
#endif
#if NET_TCP_SHUTDOWN_TIMEOUT < 1
#error "TCP shutdown timeout must be positive"
#endif
#if NET_DEFAULT_SOCKET_BUFFER < 1
#error "default socket buffer must be positive"
#endif

#endif /* !__KERN_NET_CONFIG_H__ */
