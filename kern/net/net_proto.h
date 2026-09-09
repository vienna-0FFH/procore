#ifndef __KERN_NET_NET_PROTO_H__
#define __KERN_NET_NET_PROTO_H__

#include <defs.h>
#include <net_config.h>

#define NET_ETH_ADDR_LEN             6
#define NET_ETH_HEADER_LEN           14
#define NET_IPV4_MIN_HEADER_LEN      20
#define NET_UDP_HEADER_LEN           8
#define NET_TCP_MIN_HEADER_LEN       20
#define NET_ARP_PACKET_LEN           28

#define NET_ETHERTYPE_IPV4           0x0800
#define NET_ETHERTYPE_ARP            0x0806

#define NET_IPPROTO_UDP              17
#define NET_IPPROTO_TCP              6

#define NET_TCP_FIN                  0x01
#define NET_TCP_SYN                  0x02
#define NET_TCP_RST                  0x04
#define NET_TCP_PSH                  0x08
#define NET_TCP_ACK                  0x10

#define NET_ARP_HTYPE_ETHERNET       1
#define NET_ARP_PTYPE_IPV4           0x0800
#define NET_ARP_OP_REQUEST            1
#define NET_ARP_OP_REPLY             2

struct net_eth_header {
    uint8_t destination[NET_ETH_ADDR_LEN];
    uint8_t source[NET_ETH_ADDR_LEN];
    uint16_t ethertype;
} __attribute__((packed));

struct net_ipv4_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t fragment_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t source;
    uint32_t destination;
} __attribute__((packed));

struct net_udp_header {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct net_tcp_header {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

struct net_arp_packet {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_length;
    uint8_t protocol_length;
    uint16_t operation;
    uint8_t sender_mac[NET_ETH_ADDR_LEN];
    uint32_t sender_ip;
    uint8_t target_mac[NET_ETH_ADDR_LEN];
    uint32_t target_ip;
} __attribute__((packed));

/* One's-complement checksum in host integer form.  The input is always a
 * network-order byte stream, so the same helper validates a received header
 * when its checksum field is included. */
uint16_t net_checksum(const void *data, size_t length);
uint16_t net_udp_checksum(uint32_t source, uint32_t destination,
                          const struct net_udp_header *udp,
                          const void *payload, size_t payload_length);
uint16_t net_tcp_checksum(uint32_t source, uint32_t destination,
                          const struct net_tcp_header *tcp,
                          const void *payload, size_t payload_length);

bool net_mac_equal(const uint8_t left[NET_ETH_ADDR_LEN],
                   const uint8_t right[NET_ETH_ADDR_LEN]);
bool net_mac_is_broadcast(const uint8_t address[NET_ETH_ADDR_LEN]);
void net_mac_copy(uint8_t destination[NET_ETH_ADDR_LEN],
                  const uint8_t source[NET_ETH_ADDR_LEN]);

int net_build_udp_frame(uint8_t *frame, size_t capacity,
                        const uint8_t source_mac[NET_ETH_ADDR_LEN],
                        const uint8_t destination_mac[NET_ETH_ADDR_LEN],
                        uint32_t source_ip, uint32_t destination_ip,
                        uint16_t source_port, uint16_t destination_port,
                        const void *payload, size_t payload_length,
                        uint16_t identification);

int net_parse_udp_frame(const uint8_t *frame, size_t length,
                        uint32_t local_ip, uint16_t *source_port,
                        uint16_t *destination_port, uint32_t *source_ip,
                        uint32_t *destination_ip, const uint8_t **payload,
                        size_t *payload_length);

int net_build_tcp_frame(uint8_t *frame, size_t capacity,
                        const uint8_t source_mac[NET_ETH_ADDR_LEN],
                        const uint8_t destination_mac[NET_ETH_ADDR_LEN],
                        uint32_t source_ip, uint32_t destination_ip,
                        uint16_t source_port, uint16_t destination_port,
                        uint32_t sequence, uint32_t acknowledgement,
                        uint8_t flags, uint16_t window,
                        const void *payload, size_t payload_length,
                        uint16_t identification);

int net_parse_tcp_frame(const uint8_t *frame, size_t length,
                        uint32_t local_ip, uint16_t *source_port,
                        uint16_t *destination_port, uint32_t *source_ip,
                        uint32_t *destination_ip, uint32_t *sequence,
                        uint32_t *acknowledgement, uint8_t *flags,
                        uint16_t *window,
                        const uint8_t **payload, size_t *payload_length);

int net_parse_arp_frame(const uint8_t *frame, size_t length,
                        struct net_arp_packet *arp);

#endif /* !__KERN_NET_NET_PROTO_H__ */
