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

struct net_socket {
    list_entry_t link;
    spinlock_t lock;
    semaphore_t rx_sem;
    list_entry_t rx_queue;
    int rx_count;
    int waiters;
    volatile int ref_count;
    volatile int descriptor_count;
    uint16_t port;
    uint32_t addr;
    bool bound;
    bool connected;
    struct sockaddr_in peer;
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
