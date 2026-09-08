#include <error.h>
#include <file.h>
#include <netdb.h>
#include <poll.h>
#include <socket.h>
#include <string.h>
#include <ulib.h>

#define DNS_PORT              53
#define DNS_PACKET_SIZE       512
#define DNS_HEADER_SIZE       12
#define DNS_TYPE_A            1
#define DNS_CLASS_IN           1

static uint16_t dns_next_id;

static uint16_t
dns_read16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void
dns_write16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static int
dns_skip_name(const uint8_t *packet, size_t length, size_t *offset_store) {
    size_t offset = *offset_store;
    size_t steps = 0;
    while (offset < length && steps++ < length) {
        uint8_t label = packet[offset++];
        if (label == 0) {
            *offset_store = offset;
            return 0;
        }
        if ((label & 0xC0) == 0xC0) {
            if (offset >= length) {
                return -E_INVAL;
            }
            *offset_store = offset + 1;
            return 0;
        }
        if ((label & 0xC0) != 0 || label > 63 ||
            offset + label > length) {
            return -E_INVAL;
        }
        offset += label;
    }
    return -E_INVAL;
}

int
dns_resolve_ipv4(const char *hostname, uint32_t *address_store) {
    uint8_t packet[DNS_PACKET_SIZE];
    struct sockaddr_in server;
    struct pollfd waitfd;
    size_t length = DNS_HEADER_SIZE;
    size_t label_start;
    const char *cursor;
    int fd;
    int ret;
    uint16_t query_id;
    uint16_t answers;
    size_t offset;
    uint16_t i;

    if (hostname == NULL || address_store == NULL || *hostname == '\0') {
        return -E_INVAL;
    }
    memset(packet, 0, sizeof(packet));
    query_id = ++dns_next_id;
    dns_write16(packet + 0, query_id);
    dns_write16(packet + 2, 0x0100);       /* recursion desired */
    dns_write16(packet + 4, 1);            /* one question */
    cursor = hostname;
    while (*cursor != '\0') {
        size_t label_length = 0;
        label_start = length++;
        while (cursor[label_length] != '\0' && cursor[label_length] != '.') {
            label_length++;
        }
        if (label_length == 0 || label_length > 63 ||
            length + label_length + 1 + 4 > sizeof(packet)) {
            return -E_INVAL;
        }
        packet[label_start] = (uint8_t)label_length;
        memcpy(packet + length, cursor, label_length);
        length += label_length;
        cursor += label_length;
        if (*cursor == '.') {
            cursor++;
        }
    }
    if (length + 1 + 4 > sizeof(packet)) {
        return -E_INVAL;
    }
    packet[length++] = 0;
    dns_write16(packet + length, DNS_TYPE_A);
    length += 2;
    dns_write16(packet + length, DNS_CLASS_IN);
    length += 2;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return fd;
    }
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(DNS_PORT);
    server.sin_addr = NET_DNS_IP;
    ret = connect(fd, &server, sizeof(server));
    if (ret == 0) {
        ret = send(fd, packet, length);
    }
    if (ret == (int)length) {
        waitfd.fd = fd;
        waitfd.events = POLLIN;
        waitfd.revents = 0;
        ret = poll(&waitfd, 1, 3000);
        if (ret > 0 && (waitfd.revents & POLLIN) != 0) {
            ret = recv(fd, packet, sizeof(packet));
        }
        else if (ret >= 0) {
            ret = -E_TIMEOUT;
        }
    }
    close(fd);
    if (ret < DNS_HEADER_SIZE) {
        return ret < 0 ? ret : -E_INVAL;
    }
    if (dns_read16(packet + 0) != query_id ||
        (dns_read16(packet + 2) & 0x8000) == 0 ||
        (dns_read16(packet + 2) & 0x000F) != 0) {
        return -E_NOENT;
    }
    if (dns_read16(packet + 4) != 1) {
        return -E_INVAL;
    }
    answers = dns_read16(packet + 6);
    offset = DNS_HEADER_SIZE;
    if (dns_skip_name(packet, (size_t)ret, &offset) != 0 ||
        offset + 4 > (size_t)ret) {
        return -E_INVAL;
    }
    offset += 4;
    for (i = 0; i < answers; i++) {
        uint16_t type, class_code, data_length;
        if (dns_skip_name(packet, (size_t)ret, &offset) != 0 ||
            offset + 10 > (size_t)ret) {
            return -E_INVAL;
        }
        type = dns_read16(packet + offset);
        class_code = dns_read16(packet + offset + 2);
        data_length = dns_read16(packet + offset + 8);
        offset += 10;
        if (offset + data_length > (size_t)ret) {
            return -E_INVAL;
        }
        if (type == DNS_TYPE_A && class_code == DNS_CLASS_IN &&
            data_length == 4) {
            memcpy(address_store, packet + offset, 4);
            return 0;
        }
        offset += data_length;
    }
    return -E_NOENT;
}
