#ifndef __KERN_NET_NET_H__
#define __KERN_NET_NET_H__

#include <defs.h>
#include <list.h>
#include <spinlock.h>
#include <sem.h>
#include <unistd.h>
#include <net_config.h>

struct net_packet {
    list_entry_t link;
    size_t len;
    struct sockaddr_in from;
    uint8_t data[1];
};

struct net_tcp_tx_segment {
    list_entry_t link;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t flags;
    bool sent;
    size_t length;
    size_t sent_tick;
    unsigned int retries;
    uint8_t data[1];
};

struct net_socket {
    list_entry_t link;
    spinlock_t lock;
    semaphore_t rx_sem;
    list_entry_t rx_queue;
    int rx_count;
    int waiters;
    volatile int ref_count;
    volatile int descriptor_count;
    int type;
    int protocol;
    uint16_t port;
    uint32_t addr;
    bool bound;
    bool connected;
    struct sockaddr_in peer;
    enum {
        NET_TCP_CLOSED,
        NET_TCP_SYN_SENT,
        NET_TCP_SYN_RECEIVED,
        NET_TCP_ESTABLISHED,
        NET_TCP_CLOSE_WAIT,
        NET_TCP_FIN_WAIT_1,
        NET_TCP_FIN_WAIT_2,
        NET_TCP_CLOSING,
        NET_TCP_LAST_ACK,
    } tcp_state;
    bool listening;
    unsigned int listen_backlog;
    unsigned int accept_count;
    int accept_waiters;
    semaphore_t accept_sem;
    list_entry_t accept_queue;
    list_entry_t accept_link;
    struct net_socket *listener;
    uint32_t tcp_snd_una;
    uint32_t tcp_snd_nxt;
    uint32_t tcp_rcv_nxt;
    int connect_waiters;
    bool tcp_eof;
    bool tcp_read_shutdown;
    bool tcp_write_shutdown;
    bool tcp_fin_sent;
    bool tcp_fin_pending;
    size_t tcp_last_tx_len;
    uint32_t tcp_last_tx_seq;
    uint32_t tcp_last_tx_ack;
    uint8_t tcp_last_tx_flags;
    size_t tcp_last_tx_tick;
    unsigned int tcp_retry_count;
    uint8_t *tcp_last_tx_payload;
    list_entry_t tcp_tx_queue;
    size_t tcp_tx_count;
    uint32_t tcp_snd_wnd;
    uint32_t tcp_cwnd;
    uint32_t tcp_ssthresh;
    uint32_t tcp_last_ack;
    unsigned int tcp_dup_acks;
    volatile bool closed;
};

void net_init(void);
void net_poll(void);
struct net_socket *net_socket_create(int domain, int type, int protocol);
void net_socket_get(struct net_socket *socket);
void net_socket_put(struct net_socket *socket);
void net_socket_get_descriptor(struct net_socket *socket);
void net_socket_close_descriptor(struct net_socket *socket);
int net_socket_bind(struct net_socket *socket,
                    const struct sockaddr_in *address, size_t length);
int net_socket_listen(struct net_socket *socket, int backlog);
int net_socket_accept(struct net_socket *socket,
                      struct net_socket **accepted_store,
                      struct sockaddr_in *address, bool nonblock);
int net_socket_shutdown(struct net_socket *socket, int how);
int net_socket_connect(struct net_socket *socket,
                       const struct sockaddr_in *address, size_t length);
int net_socket_sendto(struct net_socket *socket, const void *data, size_t length,
                      const struct sockaddr_in *destination, size_t dest_length);
int net_socket_recvfrom(struct net_socket *socket, void *data, size_t length,
                        struct sockaddr_in *source, size_t *source_length,
                        bool nonblock);
int net_socket_send(struct net_socket *socket, const void *data, size_t length);
int net_socket_recv(struct net_socket *socket, void *data, size_t length,
                    bool nonblock);
int net_socket_getsockname(struct net_socket *socket,
                           struct sockaddr_in *address);
int net_socket_getpeername(struct net_socket *socket,
                           struct sockaddr_in *address);
int net_socket_poll(struct net_socket *socket, int16_t events,
                    int16_t *revents_store);
void net_get_stats(struct net_stats *stats);

#endif /* !__KERN_NET_NET_H__ */
