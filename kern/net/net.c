#include <defs.h>
#include <error.h>
#include <e1000.h>
#include <clock.h>
#include <kmalloc.h>
#include <net.h>
#include <net_proto.h>
#include <stdio.h>
#include <string.h>

#define NET_FRAME_CAPACITY          (NET_ETH_HEADER_LEN + NET_MTU)

struct net_arp_cache_entry {
    uint32_t ip;
    uint8_t mac[NET_ETH_ADDR_LEN];
    size_t expires;
    bool valid;
};

static list_entry_t net_socket_list;
static spinlock_t net_socket_lock;
static spinlock_t net_device_lock;
static uint32_t net_next_port;
static struct net_stats net_statistics;
static bool net_ready;
static uint8_t net_local_mac[NET_ETH_ADDR_LEN];
static struct net_arp_cache_entry net_arp_cache[NET_ARP_CACHE_SIZE];
static uint32_t net_ip_identification;

static struct net_socket *
net_find_port_locked(uint16_t port, struct net_socket *exclude);
static struct net_socket *
net_find_port_addr_locked(uint16_t port, uint32_t addr);

static int
net_queue_packet(struct net_socket *socket, const void *data, size_t length,
                 uint32_t source_ip, uint16_t source_port) {
    struct net_packet *packet;
    if (socket == NULL || data == NULL || length == 0 ||
        length > NET_MAX_DATAGRAM) {
        return -E_INVAL;
    }
    packet = kmalloc(sizeof(*packet) + length - 1);
    if (packet == NULL) {
        atomic_inc_return((volatile int *)&net_statistics.dropped_packets);
        return -E_NO_MEM;
    }
    list_init(&packet->link);
    packet->len = length;
    packet->from.sin_family = AF_INET;
    packet->from.sin_port = source_port;
    packet->from.sin_addr = source_ip;
    memset(packet->from.sin_zero, 0, sizeof(packet->from.sin_zero));
    memcpy(packet->data, data, length);
    spin_lock(&socket->lock);
    if (socket->closed || socket->rx_count >= NET_RX_QUEUE_LIMIT) {
        spin_unlock(&socket->lock);
        kfree(packet);
        atomic_inc_return((volatile int *)&net_statistics.dropped_packets);
        return -E_BUSY;
    }
    list_add(&socket->rx_queue, &packet->link);
    socket->rx_count++;
    spin_unlock(&socket->lock);
    up(&socket->rx_sem);
    return 0;
}

static bool
net_arp_lookup_locked(uint32_t ip, uint8_t mac[NET_ETH_ADDR_LEN]) {
    size_t i;
    for (i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        if (net_arp_cache[i].valid && net_arp_cache[i].ip == ip) {
            if (net_arp_cache[i].expires >= ticks) {
                net_mac_copy(mac, net_arp_cache[i].mac);
                return 1;
            }
            net_arp_cache[i].valid = 0;
        }
    }
    return 0;
}

static void
net_arp_update_locked(uint32_t ip, const uint8_t mac[NET_ETH_ADDR_LEN]) {
    size_t i;
    size_t slot = 0;
    size_t oldest = (size_t)-1;
    size_t oldest_tick = (size_t)-1;
    if (ip == INADDR_ANY || net_mac_is_broadcast(mac)) {
        return;
    }
    for (i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        if (net_arp_cache[i].valid && net_arp_cache[i].ip == ip) {
            slot = i;
            goto found;
        }
        if (!net_arp_cache[i].valid) {
            slot = i;
            goto found;
        }
        if (net_arp_cache[i].expires < oldest_tick) {
            oldest_tick = net_arp_cache[i].expires;
            oldest = i;
        }
    }
    if (oldest != (size_t)-1) {
        slot = oldest;
    }
found:
    net_arp_cache[slot].ip = ip;
    net_mac_copy(net_arp_cache[slot].mac, mac);
    net_arp_cache[slot].expires = ticks + NET_ARP_TTL_TICKS;
    net_arp_cache[slot].valid = 1;
}

static int
net_send_frame(const uint8_t *frame, size_t length) {
    uint8_t padded[NET_MTU];
    int ret;
    if (!e1000_present() || frame == NULL || length == 0 || length > NET_MTU) {
        return -E_NA_DEV;
    }
    if (length < NET_ETH_MIN_FRAME) {
        memcpy(padded, frame, length);
        memset(padded + length, 0, NET_ETH_MIN_FRAME - length);
        frame = padded;
        length = NET_ETH_MIN_FRAME;
    }
    spin_lock(&net_device_lock);
    ret = e1000_transmit(frame, length);
    spin_unlock(&net_device_lock);
    return ret;
}

static int
net_send_arp_request(uint32_t target_ip) {
    uint8_t frame[NET_FRAME_CAPACITY];
    struct net_eth_header *ethernet;
    struct net_arp_packet *arp;
    static const uint8_t broadcast[NET_ETH_ADDR_LEN] =
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    ethernet = (struct net_eth_header *)frame;
    net_mac_copy(ethernet->destination, broadcast);
    net_mac_copy(ethernet->source, net_local_mac);
    ethernet->ethertype = htons(NET_ETHERTYPE_ARP);
    arp = (struct net_arp_packet *)(frame + NET_ETH_HEADER_LEN);
    arp->hardware_type = htons(NET_ARP_HTYPE_ETHERNET);
    arp->protocol_type = htons(NET_ARP_PTYPE_IPV4);
    arp->hardware_length = NET_ETH_ADDR_LEN;
    arp->protocol_length = sizeof(uint32_t);
    arp->operation = htons(NET_ARP_OP_REQUEST);
    net_mac_copy(arp->sender_mac, net_local_mac);
    arp->sender_ip = NET_LOCAL_IP;
    memset(arp->target_mac, 0, sizeof(arp->target_mac));
    arp->target_ip = target_ip;
    return net_send_frame(frame, NET_ETH_HEADER_LEN + sizeof(*arp));
}

static int
net_resolve_arp(uint32_t target_ip, uint8_t mac[NET_ETH_ADDR_LEN]) {
    unsigned int attempt;
    size_t start_tick;
    for (attempt = 0; attempt < NET_ARP_REQUEST_RETRIES; attempt++) {
        spin_lock(&net_device_lock);
        if (net_arp_lookup_locked(target_ip, mac)) {
            spin_unlock(&net_device_lock);
            return 0;
        }
        spin_unlock(&net_device_lock);
        {
            int arp_ret = net_send_arp_request(target_ip);
            if (arp_ret < 0) {
                cprintf("net: ARP request send failed for %08x (%d)\n",
                        target_ip, arp_ret);
                return -E_NA_DEV;
            }
        }
        start_tick = ticks;
        while ((size_t)(ticks - start_tick) < NET_ARP_WAIT_TICKS) {
            net_poll();
            spin_lock(&net_device_lock);
            if (net_arp_lookup_locked(target_ip, mac)) {
                spin_unlock(&net_device_lock);
                return 0;
            }
            spin_unlock(&net_device_lock);
            asm volatile ("pause");
        }
    }
    {
        struct e1000_stats hardware;
        e1000_get_stats(&hardware);
        cprintf("net: ARP resolution timeout for %08x (hw tx=%u rx=%u err=%u)\n",
                target_ip, hardware.tx_packets, hardware.rx_packets,
                hardware.rx_errors);
    }
    return -E_NOENT;
}

static void
net_send_arp_reply(const struct net_arp_packet *request,
                   const uint8_t destination_mac[NET_ETH_ADDR_LEN]) {
    uint8_t frame[NET_FRAME_CAPACITY];
    struct net_eth_header *ethernet;
    struct net_arp_packet *arp;

    ethernet = (struct net_eth_header *)frame;
    net_mac_copy(ethernet->destination, destination_mac);
    net_mac_copy(ethernet->source, net_local_mac);
    ethernet->ethertype = htons(NET_ETHERTYPE_ARP);
    arp = (struct net_arp_packet *)(frame + NET_ETH_HEADER_LEN);
    arp->hardware_type = htons(NET_ARP_HTYPE_ETHERNET);
    arp->protocol_type = htons(NET_ARP_PTYPE_IPV4);
    arp->hardware_length = NET_ETH_ADDR_LEN;
    arp->protocol_length = sizeof(uint32_t);
    arp->operation = htons(NET_ARP_OP_REPLY);
    net_mac_copy(arp->sender_mac, net_local_mac);
    arp->sender_ip = NET_LOCAL_IP;
    net_mac_copy(arp->target_mac, request->sender_mac);
    arp->target_ip = request->sender_ip;
    (void)net_send_frame(frame, NET_ETH_HEADER_LEN + sizeof(*arp));
}

static void
net_receive_frame(const uint8_t *frame, size_t length) {
    const struct net_eth_header *ethernet;
    struct net_arp_packet arp;
    uint16_t source_port, destination_port;
    uint32_t source_ip, destination_ip;
    const uint8_t *payload;
    size_t payload_length;
    struct net_socket *socket;

    if (frame == NULL || length < NET_ETH_HEADER_LEN) {
        return;
    }
    ethernet = (const struct net_eth_header *)frame;
    if (!net_mac_equal(ethernet->destination, net_local_mac) &&
        !net_mac_is_broadcast(ethernet->destination)) {
        return;
    }
    if (ntohs(ethernet->ethertype) == NET_ETHERTYPE_ARP) {
        if (net_parse_arp_frame(frame, length, &arp) != 0) {
            return;
        }
        spin_lock(&net_device_lock);
        net_arp_update_locked(arp.sender_ip, arp.sender_mac);
        spin_unlock(&net_device_lock);
        if (ntohs(arp.operation) == NET_ARP_OP_REQUEST &&
            arp.target_ip == NET_LOCAL_IP) {
            net_send_arp_reply(&arp, arp.sender_mac);
        }
        return;
    }
    if (ntohs(ethernet->ethertype) != NET_ETHERTYPE_IPV4 ||
        net_parse_udp_frame(frame, length, NET_LOCAL_IP, &source_port,
                            &destination_port, &source_ip, &destination_ip,
                            &payload, &payload_length) != 0) {
        return;
    }
    spin_lock(&net_device_lock);
    net_arp_update_locked(source_ip, ethernet->source);
    spin_unlock(&net_device_lock);
    spin_lock(&net_socket_lock);
    socket = net_find_port_addr_locked(destination_port, destination_ip);
    if (socket != NULL) {
        net_socket_get(socket);
    }
    spin_unlock(&net_socket_lock);
    if (socket != NULL) {
        int queued = net_queue_packet(socket, payload, payload_length,
                                      source_ip, source_port);
        net_socket_put(socket);
        if (queued == 0) {
            atomic_inc_return((volatile int *)&net_statistics.rx_packets);
        }
    }
}

static bool
net_address_valid(const struct sockaddr_in *address, size_t length) {
    return address != NULL && length >= sizeof(*address) &&
           address->sin_family == AF_INET &&
           (address->sin_addr == INADDR_ANY ||
            address->sin_addr == INADDR_LOOPBACK ||
            address->sin_addr == NET_LOCAL_IP);
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

static struct net_socket *
net_find_port_addr_locked(uint16_t port, uint32_t addr) {
    list_entry_t *entry = &net_socket_list;
    while ((entry = list_next(entry)) != &net_socket_list) {
        struct net_socket *socket = to_struct(entry, struct net_socket, link);
        if (socket->bound && socket->port == port &&
            (socket->addr == INADDR_ANY || socket->addr == addr)) {
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
        uint16_t candidate = htons((uint16_t)net_next_port);
        if (net_next_port == NET_EPHEMERAL_LAST) {
            net_next_port = NET_EPHEMERAL_FIRST;
        }
        else {
            net_next_port++;
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
    spin_init(&net_device_lock);
    net_next_port = NET_EPHEMERAL_FIRST;
    net_ip_identification = 1;
    memset(net_arp_cache, 0, sizeof(net_arp_cache));
    e1000_get_mac(net_local_mac);
    memset(&net_statistics, 0, sizeof(net_statistics));
    net_ready = 1;
    {
        struct e1000_stats hardware;
        e1000_get_stats(&hardware);
        net_statistics.hw_devices = hardware.present;
        net_statistics.hw_link_up = hardware.link_up;
        net_statistics.hw_tx_packets = hardware.tx_packets;
        net_statistics.hw_rx_packets = hardware.rx_packets;
        net_statistics.hw_tx_errors = hardware.tx_errors;
        net_statistics.hw_rx_errors = hardware.rx_errors;
    }
    cprintf("net: IPv4 UDP loopback ready\n");
}

void
net_poll(void) {
    uint8_t frame[NET_FRAME_CAPACITY];
    unsigned int budget = NET_RX_POLL_BUDGET;
    int length;

    if (!net_ready || !e1000_present()) {
        return;
    }
    while (budget-- != 0) {
        length = e1000_receive(frame, sizeof(frame));
        if (length <= 0) {
            break;
        }
        net_receive_frame(frame, (size_t)length);
    }
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
    socket->waiters = 0;
    socket->ref_count = 1;
    socket->descriptor_count = 1;
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
net_socket_get_descriptor(struct net_socket *socket) {
    if (socket != NULL) {
        atomic_inc_return(&socket->descriptor_count);
        net_socket_get(socket);
    }
}

void
net_socket_close_descriptor(struct net_socket *socket) {
    int waiters = 0;
    if (socket == NULL ||
        atomic_dec_return(&socket->descriptor_count) != 0) {
        return;
    }
    spin_lock(&socket->lock);
    socket->closed = 1;
    waiters = socket->waiters;
    spin_unlock(&socket->lock);
    while (waiters-- > 0) {
        up(&socket->rx_sem);
    }
}

void
net_socket_put(struct net_socket *socket) {
    list_entry_t *entry;

    if (socket == NULL || atomic_dec_return(&socket->ref_count) != 0) {
        return;
    }
    socket->closed = 1;
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
    uint16_t port;
    bool hardware = 0;
    uint32_t next_hop;
    uint8_t destination_mac[NET_ETH_ADDR_LEN];
    uint8_t frame[NET_FRAME_CAPACITY];
    int frame_length;
    int ret;

    if (socket == NULL || data == NULL || length == 0 ||
        length > NET_MAX_DATAGRAM ||
        destination == NULL || dest_length < sizeof(*destination) ||
        destination->sin_family != AF_INET ||
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
    hardware = e1000_present() &&
               destination->sin_addr != INADDR_ANY &&
               destination->sin_addr != INADDR_LOOPBACK &&
               destination->sin_addr != NET_LOCAL_IP;
    target = hardware ? NULL : net_find_port_addr_locked(
        destination->sin_port, destination->sin_addr);
    if (target != NULL) {
        net_socket_get(target);
    }
    spin_unlock(&net_socket_lock);
    if (hardware) {
        next_hop = (destination->sin_addr & NET_NETMASK) ==
                   (NET_LOCAL_IP & NET_NETMASK) ?
                   destination->sin_addr : NET_GATEWAY_IP;
        if (net_resolve_arp(next_hop, destination_mac) != 0) {
            return -E_NOENT;
        }
        frame_length = net_build_udp_frame(
            frame, sizeof(frame), net_local_mac, destination_mac,
            NET_LOCAL_IP, destination->sin_addr, socket->port,
            destination->sin_port, data, length, ++net_ip_identification);
        if (frame_length < 0) {
            return frame_length;
        }
        ret = net_send_frame(frame, (size_t)frame_length);
        if (ret < 0) {
            return ret;
        }
        atomic_inc_return((volatile int *)&net_statistics.tx_packets);
        return (int)length;
    }
    if (target == NULL) {
        return -E_NOENT;
    }

    ret = net_queue_packet(target, data, length,
                           socket->addr == INADDR_ANY ? INADDR_LOOPBACK :
                           socket->addr, socket->port);
    net_socket_put(target);
    if (ret != 0) {
        return ret;
    }
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
        spin_lock(&socket->lock);
        if (socket->closed) {
            spin_unlock(&socket->lock);
            return -E_BAD_PROC;
        }
        socket->waiters++;
        spin_unlock(&socket->lock);
        down(&socket->rx_sem);
        spin_lock(&socket->lock);
        socket->waiters--;
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
    struct e1000_stats hardware;
    if (stats == NULL) {
        return;
    }
    e1000_get_stats(&hardware);
    spin_lock(&net_socket_lock);
    *stats = net_statistics;
    stats->hw_devices = hardware.present;
    stats->hw_link_up = hardware.link_up;
    stats->hw_tx_packets = hardware.tx_packets;
    stats->hw_rx_packets = hardware.rx_packets;
    stats->hw_tx_errors = hardware.tx_errors;
    stats->hw_rx_errors = hardware.rx_errors;
    spin_unlock(&net_socket_lock);
}
