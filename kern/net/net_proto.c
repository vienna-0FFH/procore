#include <defs.h>
#include <error.h>
#include <net_proto.h>
#include <string.h>
#include <unistd.h>

static uint32_t
net_checksum_sum(const uint8_t *data, size_t length, uint32_t sum) {
    while (length >= 2) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length != 0) {
        sum += (uint32_t)data[0] << 8;
    }
    return sum;
}

static uint32_t
net_htonl(uint32_t value) {
    return ((value & 0x000000FFU) << 24) |
           ((value & 0x0000FF00U) << 8) |
           ((value & 0x00FF0000U) >> 8) |
           ((value & 0xFF000000U) >> 24);
}

static uint32_t
net_ntohl(uint32_t value) {
    return net_htonl(value);
}

uint16_t
net_checksum(const void *data, size_t length) {
    uint32_t sum = net_checksum_sum((const uint8_t *)data, length, 0);
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

uint16_t
net_udp_checksum(uint32_t source, uint32_t destination,
                 const struct net_udp_header *udp,
                 const void *payload, size_t payload_length) {
    uint8_t pseudo[12];
    uint32_t sum;
    struct net_udp_header header;

    memcpy(pseudo + 0, &source, sizeof(source));
    memcpy(pseudo + 4, &destination, sizeof(destination));
    pseudo[8] = 0;
    pseudo[9] = NET_IPPROTO_UDP;
    pseudo[10] = (uint8_t)((sizeof(*udp) + payload_length) >> 8);
    pseudo[11] = (uint8_t)(sizeof(*udp) + payload_length);
    header = *udp;
    header.checksum = 0;
    sum = net_checksum_sum(pseudo, sizeof(pseudo), 0);
    sum = net_checksum_sum((const uint8_t *)&header, sizeof(header), sum);
    sum = net_checksum_sum((const uint8_t *)payload, payload_length, sum);
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)~sum == 0 ? 0xFFFF : (uint16_t)~sum;
}

uint16_t
net_tcp_checksum(uint32_t source, uint32_t destination,
                 const struct net_tcp_header *tcp,
                 const void *payload, size_t payload_length) {
    uint8_t pseudo[12];
    uint8_t header_bytes[60];
    uint32_t sum;
    size_t header_length;

    if (tcp == NULL || (payload_length != 0 && payload == NULL)) {
        return 0;
    }
    header_length = (size_t)(tcp->data_offset >> 4) * 4;
    if (header_length < NET_TCP_MIN_HEADER_LEN || header_length > 60) {
        return 0;
    }
    memcpy(pseudo + 0, &source, sizeof(source));
    memcpy(pseudo + 4, &destination, sizeof(destination));
    pseudo[8] = 0;
    pseudo[9] = NET_IPPROTO_TCP;
    pseudo[10] = (uint8_t)((header_length + payload_length) >> 8);
    pseudo[11] = (uint8_t)(header_length + payload_length);
    memcpy(header_bytes, tcp, header_length);
    header_bytes[16] = 0;
    header_bytes[17] = 0;
    sum = net_checksum_sum(pseudo, sizeof(pseudo), 0);
    sum = net_checksum_sum(header_bytes, header_length, sum);
    sum = net_checksum_sum((const uint8_t *)payload, payload_length, sum);
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)~sum == 0 ? 0xFFFF : (uint16_t)~sum;
}

bool
net_mac_equal(const uint8_t left[NET_ETH_ADDR_LEN],
              const uint8_t right[NET_ETH_ADDR_LEN]) {
    return memcmp(left, right, NET_ETH_ADDR_LEN) == 0;
}

bool
net_mac_is_broadcast(const uint8_t address[NET_ETH_ADDR_LEN]) {
    static const uint8_t broadcast[NET_ETH_ADDR_LEN] =
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    return net_mac_equal(address, broadcast);
}

void
net_mac_copy(uint8_t destination[NET_ETH_ADDR_LEN],
             const uint8_t source[NET_ETH_ADDR_LEN]) {
    memcpy(destination, source, NET_ETH_ADDR_LEN);
}

int
net_build_udp_frame(uint8_t *frame, size_t capacity,
                    const uint8_t source_mac[NET_ETH_ADDR_LEN],
                    const uint8_t destination_mac[NET_ETH_ADDR_LEN],
                    uint32_t source_ip, uint32_t destination_ip,
                    uint16_t source_port, uint16_t destination_port,
                    const void *payload, size_t payload_length,
                    uint16_t identification) {
    struct net_eth_header *ethernet;
    struct net_ipv4_header *ip;
    struct net_udp_header *udp;
    size_t ip_length = NET_IPV4_MIN_HEADER_LEN +
                       NET_UDP_HEADER_LEN + payload_length;
    size_t frame_length = NET_ETH_HEADER_LEN + ip_length;

    if (frame == NULL || source_mac == NULL || destination_mac == NULL ||
        payload == NULL || payload_length == 0 ||
        payload_length > 0xFFFFU - NET_UDP_HEADER_LEN ||
        ip_length > 0xFFFFU || frame_length > capacity) {
        return -E_INVAL;
    }
    ethernet = (struct net_eth_header *)frame;
    net_mac_copy(ethernet->source, source_mac);
    net_mac_copy(ethernet->destination, destination_mac);
    ethernet->ethertype = htons(NET_ETHERTYPE_IPV4);

    ip = (struct net_ipv4_header *)(frame + NET_ETH_HEADER_LEN);
    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->total_length = htons((uint16_t)ip_length);
    ip->identification = htons(identification);
    ip->fragment_offset = 0;
    ip->ttl = NET_IP_TTL;
    ip->protocol = NET_IPPROTO_UDP;
    ip->source = source_ip;
    ip->destination = destination_ip;
    ip->checksum = htons(net_checksum(ip, NET_IPV4_MIN_HEADER_LEN));

    udp = (struct net_udp_header *)((uint8_t *)ip +
                                    NET_IPV4_MIN_HEADER_LEN);
    udp->source_port = source_port;
    udp->destination_port = destination_port;
    udp->length = htons((uint16_t)(NET_UDP_HEADER_LEN + payload_length));
    udp->checksum = 0;
    memcpy((uint8_t *)udp + NET_UDP_HEADER_LEN, payload, payload_length);
    udp->checksum = htons(net_udp_checksum(source_ip, destination_ip, udp,
                                           (uint8_t *)udp + NET_UDP_HEADER_LEN,
                                           payload_length));
    return (int)frame_length;
}

int
net_parse_udp_frame(const uint8_t *frame, size_t length, uint32_t local_ip,
                    uint16_t *source_port, uint16_t *destination_port,
                    uint32_t *source_ip, uint32_t *destination_ip,
                    const uint8_t **payload, size_t *payload_length) {
    const struct net_eth_header *ethernet;
    const struct net_ipv4_header *ip;
    const struct net_udp_header *udp;
    size_t ip_header_length;
    size_t ip_length;
    size_t udp_length;
    uint16_t received_checksum;
    uint16_t calculated_checksum;

    if (frame == NULL || length < NET_ETH_HEADER_LEN +
        NET_IPV4_MIN_HEADER_LEN + NET_UDP_HEADER_LEN ||
        source_port == NULL || destination_port == NULL ||
        source_ip == NULL || destination_ip == NULL || payload == NULL ||
        payload_length == NULL) {
        return -E_INVAL;
    }
    ethernet = (const struct net_eth_header *)frame;
    if (ntohs(ethernet->ethertype) != NET_ETHERTYPE_IPV4) {
        return -E_INVAL;
    }
    ip = (const struct net_ipv4_header *)(frame + NET_ETH_HEADER_LEN);
    if ((ip->version_ihl >> 4) != 4 ||
        (ip->version_ihl & 0x0F) < 5 || ip->protocol != NET_IPPROTO_UDP) {
        return -E_INVAL;
    }
    ip_header_length = (size_t)(ip->version_ihl & 0x0F) * 4;
    if (length < NET_ETH_HEADER_LEN + ip_header_length ||
        net_checksum(ip, ip_header_length) != 0) {
        return -E_INVAL;
    }
    ip_length = ntohs(ip->total_length);
    if (ip_length < ip_header_length + NET_UDP_HEADER_LEN ||
        ip_length > length - NET_ETH_HEADER_LEN ||
        (local_ip != INADDR_ANY && ip->destination != local_ip)) {
        return -E_NOENT;
    }
    udp = (const struct net_udp_header *)((const uint8_t *)ip +
                                          ip_header_length);
    udp_length = ntohs(udp->length);
    if (udp_length < NET_UDP_HEADER_LEN ||
        udp_length > ip_length - ip_header_length) {
        return -E_INVAL;
    }
    received_checksum = ntohs(udp->checksum);
    calculated_checksum = net_udp_checksum(
        ip->source, ip->destination, udp,
        (const uint8_t *)udp + NET_UDP_HEADER_LEN,
        udp_length - NET_UDP_HEADER_LEN);
    if (received_checksum != 0 && received_checksum != calculated_checksum) {
        return -E_INVAL;
    }
    *source_port = udp->source_port;
    *destination_port = udp->destination_port;
    *source_ip = ip->source;
    *destination_ip = ip->destination;
    *payload = (const uint8_t *)udp + NET_UDP_HEADER_LEN;
    *payload_length = udp_length - NET_UDP_HEADER_LEN;
    return 0;
}

int
net_parse_arp_frame(const uint8_t *frame, size_t length,
                    struct net_arp_packet *arp) {
    const struct net_eth_header *ethernet;

    if (frame == NULL || arp == NULL || length < NET_ETH_HEADER_LEN +
        sizeof(*arp)) {
        return -E_INVAL;
    }
    ethernet = (const struct net_eth_header *)frame;
    if (ntohs(ethernet->ethertype) != NET_ETHERTYPE_ARP) {
        return -E_INVAL;
    }
    memcpy(arp, frame + NET_ETH_HEADER_LEN, sizeof(*arp));
    if (ntohs(arp->hardware_type) != NET_ARP_HTYPE_ETHERNET ||
        ntohs(arp->protocol_type) != NET_ARP_PTYPE_IPV4 ||
        arp->hardware_length != NET_ETH_ADDR_LEN ||
        arp->protocol_length != sizeof(uint32_t) ||
        (ntohs(arp->operation) != NET_ARP_OP_REQUEST &&
         ntohs(arp->operation) != NET_ARP_OP_REPLY)) {
        return -E_INVAL;
    }
    return 0;
}

int
net_build_tcp_frame(uint8_t *frame, size_t capacity,
                    const uint8_t source_mac[NET_ETH_ADDR_LEN],
                    const uint8_t destination_mac[NET_ETH_ADDR_LEN],
                    uint32_t source_ip, uint32_t destination_ip,
                    uint16_t source_port, uint16_t destination_port,
                    uint32_t sequence, uint32_t acknowledgement,
                    uint8_t flags, uint16_t window,
                    const void *payload, size_t payload_length,
                    uint16_t identification) {
    struct net_eth_header *ethernet;
    struct net_ipv4_header *ip;
    struct net_tcp_header *tcp;
    size_t ip_length = NET_IPV4_MIN_HEADER_LEN +
                       NET_TCP_MIN_HEADER_LEN + payload_length;
    size_t frame_length = NET_ETH_HEADER_LEN + ip_length;

    if (frame == NULL || source_mac == NULL || destination_mac == NULL ||
        (payload_length != 0 && payload == NULL) ||
        payload_length > 0xFFFFU - NET_TCP_MIN_HEADER_LEN ||
        ip_length > 0xFFFFU || frame_length > capacity) {
        return -E_INVAL;
    }
    ethernet = (struct net_eth_header *)frame;
    net_mac_copy(ethernet->source, source_mac);
    net_mac_copy(ethernet->destination, destination_mac);
    ethernet->ethertype = htons(NET_ETHERTYPE_IPV4);
    ip = (struct net_ipv4_header *)(frame + NET_ETH_HEADER_LEN);
    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->total_length = htons((uint16_t)ip_length);
    ip->identification = htons(identification);
    ip->ttl = NET_IP_TTL;
    ip->protocol = NET_IPPROTO_TCP;
    ip->source = source_ip;
    ip->destination = destination_ip;
    ip->checksum = htons(net_checksum(ip, NET_IPV4_MIN_HEADER_LEN));
    tcp = (struct net_tcp_header *)((uint8_t *)ip +
                                    NET_IPV4_MIN_HEADER_LEN);
    memset(tcp, 0, sizeof(*tcp));
    tcp->source_port = source_port;
    tcp->destination_port = destination_port;
    tcp->sequence = net_htonl(sequence);
    tcp->acknowledgement = net_htonl(acknowledgement);
    tcp->data_offset = (NET_TCP_MIN_HEADER_LEN / 4) << 4;
    tcp->flags = flags;
    tcp->window = window;
    if (payload_length != 0) {
        memcpy((uint8_t *)tcp + NET_TCP_MIN_HEADER_LEN,
               payload, payload_length);
    }
    tcp->checksum = htons(net_tcp_checksum(source_ip, destination_ip, tcp,
                                           payload, payload_length));
    return (int)frame_length;
}

int
net_parse_tcp_frame(const uint8_t *frame, size_t length,
                    uint32_t local_ip, uint16_t *source_port,
                    uint16_t *destination_port, uint32_t *source_ip,
                    uint32_t *destination_ip, uint32_t *sequence,
                    uint32_t *acknowledgement, uint8_t *flags,
                    uint16_t *window,
                    const uint8_t **payload, size_t *payload_length) {
    const struct net_eth_header *ethernet;
    const struct net_ipv4_header *ip;
    const struct net_tcp_header *tcp;
    size_t ip_header_length, ip_length, tcp_header_length, tcp_length;
    uint16_t received_checksum, calculated_checksum;

    if (frame == NULL || length < NET_ETH_HEADER_LEN +
        NET_IPV4_MIN_HEADER_LEN + NET_TCP_MIN_HEADER_LEN ||
        source_port == NULL || destination_port == NULL ||
        source_ip == NULL || destination_ip == NULL || sequence == NULL ||
        acknowledgement == NULL || flags == NULL || window == NULL ||
        payload == NULL ||
        payload_length == NULL) {
        return -E_INVAL;
    }
    ethernet = (const struct net_eth_header *)frame;
    if (ntohs(ethernet->ethertype) != NET_ETHERTYPE_IPV4) {
        return -E_INVAL;
    }
    ip = (const struct net_ipv4_header *)(frame + NET_ETH_HEADER_LEN);
    if ((ip->version_ihl >> 4) != 4 ||
        (ip->version_ihl & 0x0F) < 5 || ip->protocol != NET_IPPROTO_TCP) {
        return -E_INVAL;
    }
    ip_header_length = (size_t)(ip->version_ihl & 0x0F) * 4;
    if (length < NET_ETH_HEADER_LEN + ip_header_length ||
        net_checksum(ip, ip_header_length) != 0) {
        return -E_INVAL;
    }
    ip_length = ntohs(ip->total_length);
    if (ip_length < ip_header_length + NET_TCP_MIN_HEADER_LEN ||
        ip_length > length - NET_ETH_HEADER_LEN ||
        (local_ip != INADDR_ANY && ip->destination != local_ip)) {
        return -E_NOENT;
    }
    tcp = (const struct net_tcp_header *)((const uint8_t *)ip +
                                          ip_header_length);
    tcp_header_length = (size_t)(tcp->data_offset >> 4) * 4;
    if (tcp_header_length < NET_TCP_MIN_HEADER_LEN ||
        tcp_header_length > ip_length - ip_header_length) {
        return -E_INVAL;
    }
    tcp_length = ip_length - ip_header_length;
    received_checksum = ntohs(tcp->checksum);
    calculated_checksum = net_tcp_checksum(ip->source, ip->destination, tcp,
        (const uint8_t *)tcp + tcp_header_length,
        tcp_length - tcp_header_length);
    if (received_checksum != calculated_checksum) {
        return -E_INVAL;
    }
    *source_port = tcp->source_port;
    *destination_port = tcp->destination_port;
    *source_ip = ip->source;
    *destination_ip = ip->destination;
    *sequence = net_ntohl(tcp->sequence);
    *acknowledgement = net_ntohl(tcp->acknowledgement);
    *flags = tcp->flags;
    *window = ntohs(tcp->window);
    *payload = (const uint8_t *)tcp + tcp_header_length;
    *payload_length = tcp_length - tcp_header_length;
    return 0;
}
