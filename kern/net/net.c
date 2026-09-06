#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <net.h>
#include <stdio.h>
#include <string.h>

static list_entry_t net_socket_list;
static spinlock_t net_socket_lock;
static uint16_t net_next_port;
static struct net_stats net_statistics;
static bool net_ready;

static bool
net_address_valid(const struct sockaddr_in *address, size_t length) {
    return address != NULL && length >= sizeof(*address) &&
           address->sin_family == AF_INET;
}

static struct net_socket *
net_find_port_locked(uint16_t port, struct net_socket *exclude) {
    list_entry_t *entry = &net_socket_list;
    while ((entry = list_next(entry)) != &net_socket_list) {
        struct net_socket *socket = to_struct(entry, struct net_socket, link);
        if (socket != exclude && socket->bound && socket->port == port) {
            return socket;
        }
    }
    return NULL;
}

static int
net_allocate_port_locked(uint16_t *port_store) {
    uint32_t count = (uint32_t)NET_EPHEMERAL_LAST - NET_EPHEMERAL_FIRST + 1;
    uint32_t i;

    for (i = 0; i < count; i++) {
        uint16_t candidate = htons(net_next_port);
        net_next_port++;
        if (net_next_port > NET_EPHEMERAL_LAST) {
            net_next_port = NET_EPHEMERAL_FIRST;
        }
        if (net_find_port_locked(candidate, NULL) == NULL) {
            *port_store = candidate;
            return 0;
        }
    }
    return -E_BUSY;
}

void
net_init(void) {
    list_init(&net_socket_list);
    spin_init(&net_socket_lock);
    net_next_port = NET_EPHEMERAL_FIRST;
    memset(&net_statistics, 0, sizeof(net_statistics));
    net_ready = 1;
    cprintf("net: IPv4 UDP loopback ready\n");
}

struct net_socket *
net_socket_create(int domain, int type, int protocol) {
    struct net_socket *socket;

    if (!net_ready || domain != AF_INET || type != SOCK_DGRAM ||
        (protocol != 0 && protocol != IPPROTO_UDP)) {
        return NULL;
    }
    socket = kmalloc(sizeof(*socket));
    if (socket == NULL) {
        return NULL;
    }
    list_init(&socket->link);
    spin_init(&socket->lock);
    sem_init(&socket->rx_sem, 0);
    list_init(&socket->rx_queue);
    socket->rx_count = 0;
    socket->ref_count = 1;
    socket->port = 0;
    socket->addr = INADDR_ANY;
    socket->bound = 0;
    socket->closed = 0;

    spin_lock(&net_socket_lock);
    if (net_statistics.sockets >= NET_MAX_SOCKETS) {
        spin_unlock(&net_socket_lock);
        kfree(socket);
        return NULL;
    }
    list_add(&net_socket_list, &socket->link);
    net_statistics.sockets++;
    spin_unlock(&net_socket_lock);
    return socket;
}

void
net_socket_get(struct net_socket *socket) {
    if (socket != NULL) {
        atomic_inc_return(&socket->ref_count);
    }
}

void
net_socket_put(struct net_socket *socket) {
    list_entry_t *entry;

    if (socket == NULL || atomic_dec_return(&socket->ref_count) != 0) {
        return;
    }
    socket->closed = 1;
    up(&socket->rx_sem);
    spin_lock(&net_socket_lock);
    if (!list_empty(&socket->link)) {
        list_del_init(&socket->link);
    }
    if (net_statistics.sockets != 0) {
        net_statistics.sockets--;
    }
    spin_unlock(&net_socket_lock);

    spin_lock(&socket->lock);
    while ((entry = list_next(&socket->rx_queue)) != &socket->rx_queue) {
        struct net_packet *packet = to_struct(entry, struct net_packet, link);
        list_del_init(entry);
        kfree(packet);
    }
    socket->rx_count = 0;
    spin_unlock(&socket->lock);
    kfree(socket);
}

int
net_socket_bind(struct net_socket *socket,
                const struct sockaddr_in *address, size_t length) {
    uint16_t port;
    int ret = 0;

    if (socket == NULL || !net_address_valid(address, length)) {
        return -E_INVAL;
    }
    port = address->sin_port;
    spin_lock(&net_socket_lock);
    if (socket->closed) {
        ret = -E_BAD_PROC;
    }
    else if (socket->bound) {
        ret = (socket->port == port && socket->addr == address->sin_addr) ?
              0 : -E_BUSY;
    }
    else if (port != 0 && net_find_port_locked(port, socket) != NULL) {
        ret = -E_BUSY;
    }
    else if (port == 0 && (ret = net_allocate_port_locked(&port)) == 0) {
        socket->port = port;
        socket->addr = address->sin_addr;
        socket->bound = 1;
    }
    else if (port != 0) {
        socket->port = port;
        socket->addr = address->sin_addr;
        socket->bound = 1;
    }
    spin_unlock(&net_socket_lock);
    return ret;
}

int
net_socket_sendto(struct net_socket *socket, const void *data, size_t length,
                  const struct sockaddr_in *destination, size_t dest_length) {
    struct net_socket *target;
    struct net_packet *packet;
    uint16_t port;

    if (socket == NULL || data == NULL || length == 0 ||
        length > NET_MAX_DATAGRAM ||
        !net_address_valid(destination, dest_length) ||
        destination->sin_port == 0) {
        return -E_INVAL;
    }
    spin_lock(&net_socket_lock);
    if (socket->closed) {
        spin_unlock(&net_socket_lock);
        return -E_BAD_PROC;
    }
    if (!socket->bound) {
        if (net_allocate_port_locked(&port) != 0) {
            spin_unlock(&net_socket_lock);
            return -E_BUSY;
        }
        socket->port = port;
        socket->addr = INADDR_LOOPBACK;
        socket->bound = 1;
    }
    target = net_find_port_locked(destination->sin_port, socket);
    if (target != NULL) {
        net_socket_get(target);
    }
    spin_unlock(&net_socket_lock);
    if (target == NULL) {
        return -E_NOENT;
    }

    packet = kmalloc(sizeof(*packet) + length - 1);
    if (packet == NULL) {
        net_socket_put(target);
        return -E_NO_MEM;
    }
    list_init(&packet->link);
    packet->len = length;
    packet->from.sin_family = AF_INET;
    packet->from.sin_port = socket->port;
    packet->from.sin_addr = socket->addr;
    memset(packet->from.sin_zero, 0, sizeof(packet->from.sin_zero));
    memcpy(packet->data, data, length);

    spin_lock(&target->lock);
    if (target->closed || target->rx_count >= NET_RX_QUEUE_LIMIT) {
        spin_unlock(&target->lock);
        kfree(packet);
        net_socket_put(target);
        atomic_inc_return((volatile int *)&net_statistics.dropped_packets);
        return -E_BUSY;
    }
    list_add(&target->rx_queue, &packet->link);
    target->rx_count++;
    spin_unlock(&target->lock);
    up(&target->rx_sem);
    net_socket_put(target);
    atomic_inc_return((volatile int *)&net_statistics.tx_packets);
    atomic_inc_return((volatile int *)&net_statistics.rx_packets);
    return (int)length;
}

int
net_socket_recvfrom(struct net_socket *socket, void *data, size_t length,
                    struct sockaddr_in *source, size_t *source_length) {
    struct net_packet *packet;
    list_entry_t *entry;
    size_t copied;

    if (socket == NULL || data == NULL || length == 0) {
        return -E_INVAL;
    }
    for (;;) {
        down(&socket->rx_sem);
        spin_lock(&socket->lock);
        if (socket->closed) {
            spin_unlock(&socket->lock);
            return -E_BAD_PROC;
        }
        entry = list_next(&socket->rx_queue);
        if (entry != &socket->rx_queue) {
            packet = to_struct(entry, struct net_packet, link);
            list_del_init(entry);
            socket->rx_count--;
            spin_unlock(&socket->lock);
            break;
        }
        spin_unlock(&socket->lock);
    }
    copied = packet->len < length ? packet->len : length;
    memcpy(data, packet->data, copied);
    if (source != NULL && source_length != NULL &&
        *source_length >= sizeof(*source)) {
        *source = packet->from;
        *source_length = sizeof(*source);
    }
    kfree(packet);
    return (int)copied;
}

void
net_get_stats(struct net_stats *stats) {
    if (stats == NULL) {
        return;
    }
    spin_lock(&net_socket_lock);
    *stats = net_statistics;
    spin_unlock(&net_socket_lock);
}
