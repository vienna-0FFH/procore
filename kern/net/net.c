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
static struct net_socket *
net_find_tcp_locked(uint16_t port, uint32_t addr, uint16_t peer_port);
static void
net_receive_tcp_frame(const uint8_t *frame, size_t length,
                      const uint8_t source_mac[NET_ETH_ADDR_LEN]);

static int
net_queue_packet(struct net_socket *socket, const void *data, size_t length,
                 uint32_t source_ip, uint16_t source_port) {
    struct net_packet *packet;
    int queue_limit;
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
    queue_limit = socket->type == SOCK_STREAM ?
                  NET_TCP_RX_QUEUE_LIMIT : NET_RX_QUEUE_LIMIT;
    spin_lock(&socket->lock);
    if (socket->closed || socket->rx_count >= queue_limit) {
        spin_unlock(&socket->lock);
        kfree(packet);
        atomic_inc_return((volatile int *)&net_statistics.dropped_packets);
        return -E_BUSY;
    }
    if (socket->type == SOCK_STREAM) {
        list_add_before(&socket->rx_queue, &packet->link);
    }
    else {
        list_add(&socket->rx_queue, &packet->link);
    }
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

static int
net_tcp_send_segment(struct net_socket *socket, uint8_t flags,
                     uint32_t sequence, uint32_t acknowledgement,
                     const void *payload, size_t payload_length) {
    uint8_t destination_mac[NET_ETH_ADDR_LEN];
    uint8_t frame[NET_FRAME_CAPACITY];
    struct sockaddr_in peer;
    uint32_t source_ip;
    uint32_t next_hop;
    int frame_length;
    int ret;

    if (socket == NULL || socket->type != SOCK_STREAM ||
        payload_length > NET_TCP_MSS) {
        return -E_INVAL;
    }
    peer = socket->peer;
    source_ip = socket->addr == INADDR_ANY ? NET_LOCAL_IP : socket->addr;
    next_hop = (peer.sin_addr & NET_NETMASK) ==
               (NET_LOCAL_IP & NET_NETMASK) ? peer.sin_addr : NET_GATEWAY_IP;
    ret = net_resolve_arp(next_hop, destination_mac);
    if (ret < 0) {
        return ret;
    }
    frame_length = net_build_tcp_frame(
        frame, sizeof(frame), net_local_mac, destination_mac,
        source_ip, peer.sin_addr, socket->port, peer.sin_port,
        sequence, acknowledgement, flags, htons(65535), payload,
        payload_length, ++net_ip_identification);
    if (frame_length < 0) {
        return frame_length;
    }
    ret = net_send_frame(frame, (size_t)frame_length);
    if (ret < 0) {
        return ret;
    }
    atomic_inc_return((volatile int *)&net_statistics.tx_packets);
    return (int)payload_length;
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
    const struct net_ipv4_header *ip;
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
        length < NET_ETH_HEADER_LEN + NET_IPV4_MIN_HEADER_LEN) {
        return;
    }
    ip = (const struct net_ipv4_header *)(frame + NET_ETH_HEADER_LEN);
    if (ip->protocol == NET_IPPROTO_TCP) {
        net_receive_tcp_frame(frame, length, ethernet->source);
        return;
    }
    if (ip->protocol != NET_IPPROTO_UDP ||
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

static bool
net_destination_valid(const struct sockaddr_in *address, size_t length) {
    return address != NULL && length >= sizeof(*address) &&
           address->sin_family == AF_INET &&
           address->sin_addr != INADDR_ANY && address->sin_port != 0;
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

static struct net_socket *
net_find_tcp_locked(uint16_t port, uint32_t addr, uint16_t peer_port) {
    list_entry_t *entry = &net_socket_list;
    while ((entry = list_next(entry)) != &net_socket_list) {
        struct net_socket *socket = to_struct(entry, struct net_socket, link);
        if (socket->type == SOCK_STREAM && socket->bound &&
            socket->port == port && socket->connected &&
            socket->peer.sin_addr == addr &&
            socket->peer.sin_port == peer_port) {
            return socket;
        }
    }
    return NULL;
}

static void
net_receive_tcp_frame(const uint8_t *frame, size_t length,
                      const uint8_t source_mac[NET_ETH_ADDR_LEN]) {
    uint16_t source_port, destination_port;
    uint32_t source_ip, destination_ip, sequence, acknowledgement;
    uint8_t flags;
    const uint8_t *payload;
    size_t payload_length;
    struct net_socket *socket;
    bool send_ack = 0;
    bool mark_eof = 0;
    bool queue_data = 0;
    int parse_ret;

    parse_ret = net_parse_tcp_frame(frame, length, NET_LOCAL_IP, &source_port,
                            &destination_port, &source_ip, &destination_ip,
                            &sequence, &acknowledgement, &flags, &payload,
                            &payload_length);
    if (parse_ret != 0) {
        return;
    }
    spin_lock(&net_device_lock);
    net_arp_update_locked(source_ip, source_mac);
    spin_unlock(&net_device_lock);
    spin_lock(&net_socket_lock);
    socket = net_find_tcp_locked(destination_port, source_ip, source_port);
    if (socket != NULL) {
        net_socket_get(socket);
    }
    spin_unlock(&net_socket_lock);
    if (socket == NULL) {
        return;
    }

    spin_lock(&socket->lock);
    if (!socket->closed && socket->tcp_state == NET_TCP_SYN_SENT &&
        (flags & (NET_TCP_SYN | NET_TCP_ACK)) ==
        (NET_TCP_SYN | NET_TCP_ACK) &&
        acknowledgement == socket->tcp_snd_nxt) {
        socket->tcp_rcv_nxt = sequence + 1;
        socket->tcp_snd_una = acknowledgement;
        socket->tcp_state = NET_TCP_ESTABLISHED;
        send_ack = 1;
    }
    else if (!socket->closed &&
             (socket->tcp_state == NET_TCP_ESTABLISHED ||
              socket->tcp_state == NET_TCP_CLOSE_WAIT)) {
        if ((flags & NET_TCP_RST) != 0) {
            socket->tcp_state = NET_TCP_CLOSED;
            socket->tcp_eof = 1;
            mark_eof = 1;
        }
        else {
            if ((flags & NET_TCP_ACK) != 0 &&
                acknowledgement > socket->tcp_snd_una &&
                acknowledgement <= socket->tcp_snd_nxt) {
                socket->tcp_snd_una = acknowledgement;
            }
            if (sequence == socket->tcp_rcv_nxt && payload_length != 0) {
                socket->tcp_rcv_nxt += (uint32_t)payload_length;
                queue_data = 1;
                send_ack = 1;
            }
            if ((flags & NET_TCP_FIN) != 0 &&
                sequence + (uint32_t)payload_length == socket->tcp_rcv_nxt) {
                socket->tcp_rcv_nxt++;
                socket->tcp_state = NET_TCP_CLOSE_WAIT;
                socket->tcp_eof = 1;
                send_ack = 1;
                mark_eof = 1;
            }
        }
    }
    spin_unlock(&socket->lock);

    if (queue_data) {
        if (net_queue_packet(socket, payload, payload_length,
                             source_ip, source_port) == 0) {
            atomic_inc_return((volatile int *)&net_statistics.rx_packets);
        }
    }
    if (send_ack) {
        uint32_t ack;
        spin_lock(&socket->lock);
        ack = socket->tcp_rcv_nxt;
        spin_unlock(&socket->lock);
        (void)net_tcp_send_segment(socket, NET_TCP_ACK,
                                   socket->tcp_snd_nxt, ack, NULL, 0);
    }
    if (mark_eof) {
        up(&socket->rx_sem);
    }
    net_socket_put(socket);
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
    cprintf("net: IPv4 UDP and active TCP client ready\n");
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

    if (!net_ready || domain != AF_INET ||
        (type != SOCK_DGRAM && type != SOCK_STREAM) ||
        (type == SOCK_DGRAM && protocol != 0 && protocol != IPPROTO_UDP) ||
        (type == SOCK_STREAM && protocol != 0 && protocol != IPPROTO_TCP)) {
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
    socket->type = type;
    socket->protocol = type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    socket->port = 0;
    socket->addr = INADDR_ANY;
    socket->bound = 0;
    socket->connected = 0;
    memset(&socket->peer, 0, sizeof(socket->peer));
    socket->tcp_state = NET_TCP_CLOSED;
    socket->tcp_snd_una = 0;
    socket->tcp_snd_nxt = 0;
    socket->tcp_rcv_nxt = 0;
    socket->connect_waiters = 0;
    socket->tcp_eof = 0;
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
net_socket_connect(struct net_socket *socket,
                   const struct sockaddr_in *address, size_t length) {
    uint16_t port;
    int ret = 0;

    if (socket == NULL || !net_destination_valid(address, length)) {
        return -E_INVAL;
    }
    if (socket->type == SOCK_STREAM) {
        size_t start_tick;
        uint32_t sequence;
        spin_lock(&net_socket_lock);
        if (socket->closed) {
            spin_unlock(&net_socket_lock);
            return -E_BAD_PROC;
        }
        if (socket->connected) {
            ret = (socket->peer.sin_addr == address->sin_addr &&
                   socket->peer.sin_port == address->sin_port &&
                   socket->tcp_state == NET_TCP_ESTABLISHED) ?
                  0 : -E_BUSY;
            spin_unlock(&net_socket_lock);
            return ret;
        }
        if (!socket->bound) {
            ret = net_allocate_port_locked(&port);
            if (ret == 0) {
                socket->port = port;
                socket->addr = INADDR_ANY;
                socket->bound = 1;
            }
        }
        if (ret == 0) {
            socket->peer = *address;
            sequence = ((uint32_t)ticks << 16) ^
                       ((uint32_t)(uintptr_t)socket >> 4);
            socket->tcp_snd_una = sequence;
            socket->tcp_snd_nxt = sequence + 1;
            socket->tcp_rcv_nxt = 0;
            socket->tcp_state = NET_TCP_SYN_SENT;
            socket->tcp_eof = 0;
            socket->connected = 1;
        }
        spin_unlock(&net_socket_lock);
        if (ret != 0) {
            return ret;
        }
        ret = net_tcp_send_segment(socket, NET_TCP_SYN, sequence, 0,
                                   NULL, 0);
        if (ret < 0) {
            spin_lock(&socket->lock);
            socket->tcp_state = NET_TCP_CLOSED;
            socket->connected = 0;
            spin_unlock(&socket->lock);
            return ret;
        }
        start_tick = ticks;
        while ((size_t)(ticks - start_tick) < NET_TCP_CONNECT_TIMEOUT) {
            enum { WAITING, CONNECTED, FAILED } state;
            spin_lock(&socket->lock);
            state = socket->tcp_state == NET_TCP_ESTABLISHED ? CONNECTED :
                    (socket->tcp_state == NET_TCP_CLOSED ? FAILED : WAITING);
            spin_unlock(&socket->lock);
            if (state == CONNECTED) {
                return 0;
            }
            if (state == FAILED) {
                return -E_NOENT;
            }
            net_poll();
            asm volatile ("pause");
        }
        spin_lock(&socket->lock);
        socket->tcp_state = NET_TCP_CLOSED;
        socket->connected = 0;
        spin_unlock(&socket->lock);
        return -E_TIMEOUT;
    }
    spin_lock(&net_socket_lock);
    if (socket->closed) {
        ret = -E_BAD_PROC;
    }
    else if (socket->connected) {
        ret = (socket->peer.sin_addr == address->sin_addr &&
               socket->peer.sin_port == address->sin_port) ?
              0 : -E_BUSY;
    }
    else {
        if (!socket->bound) {
            ret = net_allocate_port_locked(&port);
            if (ret == 0) {
                socket->port = port;
                socket->addr = INADDR_ANY;
                socket->bound = 1;
            }
        }
        if (ret == 0) {
            socket->peer = *address;
            socket->connected = 1;
        }
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

    if (socket == NULL || socket->type != SOCK_DGRAM || data == NULL || length == 0 ||
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
                    struct sockaddr_in *source, size_t *source_length,
                    bool nonblock) {
    struct net_packet *packet;
    list_entry_t *entry;
    size_t copied;

    if (socket == NULL || socket->type != SOCK_DGRAM ||
        data == NULL || length == 0) {
        return -E_INVAL;
    }
    for (;;) {
        spin_lock(&socket->lock);
        if (socket->closed) {
            spin_unlock(&socket->lock);
            return -E_BAD_PROC;
        }
        if (nonblock && socket->rx_count == 0) {
            spin_unlock(&socket->lock);
            return -E_BUSY;
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

int
net_socket_send(struct net_socket *socket, const void *data, size_t length) {
    struct sockaddr_in peer;
    size_t offset = 0;
    if (socket == NULL) {
        return -E_INVAL;
    }
    if (socket->type == SOCK_STREAM) {
        if (data == NULL || length == 0) {
            return -E_INVAL;
        }
        while (offset < length) {
            size_t chunk = length - offset;
            uint32_t sequence, acknowledgement;
            if (chunk > NET_TCP_MSS) {
                chunk = NET_TCP_MSS;
            }
            spin_lock(&socket->lock);
            if (socket->closed || socket->tcp_state != NET_TCP_ESTABLISHED) {
                spin_unlock(&socket->lock);
                return offset != 0 ? (int)offset : -E_BAD_PROC;
            }
            sequence = socket->tcp_snd_nxt;
            acknowledgement = socket->tcp_rcv_nxt;
            socket->tcp_snd_nxt += (uint32_t)chunk;
            spin_unlock(&socket->lock);
            if (net_tcp_send_segment(socket, NET_TCP_ACK | NET_TCP_PSH,
                                     sequence, acknowledgement,
                                     (const uint8_t *)data + offset,
                                     chunk) < 0) {
                return offset != 0 ? (int)offset : -E_NA_DEV;
            }
            offset += chunk;
        }
        return (int)offset;
    }
    spin_lock(&net_socket_lock);
    if (!socket->connected) {
        spin_unlock(&net_socket_lock);
        return -E_INVAL;
    }
    peer = socket->peer;
    spin_unlock(&net_socket_lock);
    return net_socket_sendto(socket, data, length, &peer, sizeof(peer));
}

int
net_socket_recv(struct net_socket *socket, void *data, size_t length,
                bool nonblock) {
    struct sockaddr_in expected;
    struct sockaddr_in source;
    int ret;
    if (socket == NULL) {
        return -E_INVAL;
    }
    if (socket->type == SOCK_STREAM) {
        struct net_packet *packet;
        list_entry_t *entry;
        for (;;) {
            spin_lock(&socket->lock);
            if (socket->rx_count != 0) {
                entry = list_next(&socket->rx_queue);
                packet = to_struct(entry, struct net_packet, link);
                list_del_init(entry);
                socket->rx_count--;
                spin_unlock(&socket->lock);
                {
                    size_t copied = packet->len < length ? packet->len : length;
                    memcpy(data, packet->data, copied);
                    if (copied < packet->len) {
                        /* The current stream queue is packet based. Keep the
                         * unread tail as a new packet for the next recv. */
                        size_t tail = packet->len - copied;
                        memmove(packet->data, packet->data + copied, tail);
                        packet->len = tail;
                        spin_lock(&socket->lock);
                        list_add_after(&socket->rx_queue, &packet->link);
                        socket->rx_count++;
                        spin_unlock(&socket->lock);
                    }
                    else {
                        kfree(packet);
                    }
                    return (int)copied;
                }
            }
            if (socket->tcp_eof) {
                spin_unlock(&socket->lock);
                return 0;
            }
            if (socket->closed || socket->tcp_state == NET_TCP_CLOSED) {
                spin_unlock(&socket->lock);
                return -E_BAD_PROC;
            }
            if (nonblock) {
                spin_unlock(&socket->lock);
                return -E_BUSY;
            }
            socket->waiters++;
            spin_unlock(&socket->lock);
            down(&socket->rx_sem);
            spin_lock(&socket->lock);
            socket->waiters--;
            spin_unlock(&socket->lock);
        }
    }
    for (;;) {
        size_t source_length = sizeof(source);
        spin_lock(&net_socket_lock);
        if (!socket->connected) {
            spin_unlock(&net_socket_lock);
            return -E_INVAL;
        }
        expected = socket->peer;
        spin_unlock(&net_socket_lock);
        ret = net_socket_recvfrom(socket, data, length, &source,
                                  &source_length, nonblock);
        if (ret < 0) {
            return ret;
        }
        if (source.sin_addr == expected.sin_addr &&
            source.sin_port == expected.sin_port) {
            return ret;
        }
        /* A connected datagram socket discards packets from other peers and
         * waits for the next matching packet without recursive stack growth. */
    }
}

int
net_socket_getsockname(struct net_socket *socket,
                       struct sockaddr_in *address) {
    if (socket == NULL || address == NULL) {
        return -E_INVAL;
    }
    spin_lock(&net_socket_lock);
    if (socket->closed || !socket->bound) {
        spin_unlock(&net_socket_lock);
        return -E_INVAL;
    }
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = socket->port;
    address->sin_addr = socket->addr;
    spin_unlock(&net_socket_lock);
    return 0;
}

int
net_socket_getpeername(struct net_socket *socket,
                       struct sockaddr_in *address) {
    if (socket == NULL || address == NULL) {
        return -E_INVAL;
    }
    spin_lock(&net_socket_lock);
    if (socket->closed || !socket->connected) {
        spin_unlock(&net_socket_lock);
        return -E_INVAL;
    }
    *address = socket->peer;
    spin_unlock(&net_socket_lock);
    return 0;
}

int
net_socket_poll(struct net_socket *socket, int16_t events,
                int16_t *revents_store) {
    int16_t revents = 0;
    if (socket == NULL || revents_store == NULL) {
        return -E_INVAL;
    }
    spin_lock(&socket->lock);
    if (socket->closed) {
        revents |= POLLHUP | POLLERR;
    }
    else if (socket->type == SOCK_STREAM) {
        if ((events & POLLIN) != 0 &&
            (socket->rx_count != 0 || socket->tcp_eof)) {
            revents |= POLLIN;
        }
        if ((events & POLLOUT) != 0 &&
            socket->tcp_state == NET_TCP_ESTABLISHED) {
            revents |= POLLOUT;
        }
    }
    else {
        if ((events & POLLIN) != 0 && socket->rx_count != 0) {
            revents |= POLLIN;
        }
        if ((events & POLLOUT) != 0) {
            revents |= POLLOUT;
        }
    }
    spin_unlock(&socket->lock);
    *revents_store = revents;
    return 0;
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
