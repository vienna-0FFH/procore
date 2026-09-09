#include <error.h>
#include <fcntl.h>
#include <file.h>
#include <netdb.h>
#include <poll.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

/* The TLS library calls this adapter instead of assuming a Unix socket API. */
static int
tls_send(void *context, const unsigned char *buffer, size_t length) {
    int ret = send(*(int *)context, buffer, length);
    if (ret == -E_BUSY) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    if (ret < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

static int
tls_recv(void *context, unsigned char *buffer, size_t length) {
    int ret = recv(*(int *)context, buffer, length);
    if (ret == -E_BUSY) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (ret < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

/* uCore has no Unix entropy device. Mix timer, TSC, PID, and address noise
 * into the DRBG seed. This is enough for the current QEMU client path; a
 * hardware RNG backend should replace it before production key generation. */
int
mbedtls_hardware_poll(void *data, unsigned char *output, size_t length,
                      size_t *olen) {
    uint32_t state = gettime_msec() ^ (uint32_t)getpid() ^
                     (uint32_t)(uintptr_t)data;
    size_t i;
    uint32_t low, high;
    (void)data;
    asm volatile ("rdtsc" : "=a" (low), "=d" (high));
    state ^= low ^ high;
    for (i = 0; i < length; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        output[i] = (unsigned char)(state >> ((i & 3) * 8));
    }
    *olen = length;
    return 0;
}

static int
tls_wait(int fd, int events) {
    struct pollfd waitfd;
    waitfd.fd = fd;
    waitfd.events = events;
    waitfd.revents = 0;
    return poll(&waitfd, 1, 5000) > 0 ? 0 : -E_TIMEOUT;
}

static int
tls_handshake(mbedtls_ssl_context *ssl, int fd) {
    int ret;
    for (;;) {
        ret = mbedtls_ssl_handshake(ssl);
        if (ret == 0) {
            return 0;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            if (tls_wait(fd, POLLIN) != 0) return -E_TIMEOUT;
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (tls_wait(fd, POLLOUT) != 0) return -E_TIMEOUT;
            continue;
        }
        return ret;
    }
}

static int
load_test_ca(mbedtls_x509_crt *certificate) {
    static unsigned char buffer[16384];
    int fd;
    int length = 0;
    int ret;
    fd = open("/https-test-root.crt", O_RDONLY);
    if (fd < 0) {
        return fd;
    }
    while (length < (int)sizeof(buffer) - 1) {
        ret = read(fd, buffer + length, sizeof(buffer) - 1 - length);
        if (ret <= 0) {
            break;
        }
        length += ret;
    }
    close(fd);
    if (length == 0 || length >= (int)sizeof(buffer) - 1) {
        return -E_INVAL;
    }
    buffer[length] = 0;
    ret = mbedtls_x509_crt_parse(certificate, buffer, (size_t)length + 1);
    return ret;
}

static int
tls_write_all(mbedtls_ssl_context *ssl, int fd,
              const unsigned char *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        int ret = mbedtls_ssl_write(ssl, buffer + offset, length - offset);
        if (ret > 0) {
            offset += (size_t)ret;
        }
        else if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            if (tls_wait(fd, POLLIN) != 0) return -E_TIMEOUT;
        }
        else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (tls_wait(fd, POLLOUT) != 0) return -E_TIMEOUT;
        }
        else {
            return ret;
        }
    }
    return 0;
}

static int
https_fetch(const char *host, const char *path) {
    static unsigned char request[512];
    unsigned char buffer[1024];
    struct sockaddr_in server;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt ca;
    bool ca_initialized = 0;
    int fd;
    int ret;
    uint32_t address;
    size_t header_length = 0;
    size_t body_received = 0;
    int body_expected = -1;
    bool response_started = 0;

#ifdef HTTPS_TEST_LOCAL
    address = NET_GATEWAY_IP;
    ret = 0;
#else
    ret = dns_resolve_ipv4(host, &address);
#endif
    if (ret != 0) {
        return ret;
    }

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return fd;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
#ifdef HTTPS_TEST_LOCAL
    server.sin_port = htons(18443);
#else
    server.sin_port = htons(443);
#endif
    server.sin_addr = address;
    /* HTTPS uses the configured gateway for the current QEMU test endpoint.
     * DNS and address selection are kept outside this first TLS milestone. */
    ret = connect(fd, &server, sizeof(server));
    if (ret != 0) goto out_fd;
    ret = fcntl(fd, F_SETFL, O_NONBLOCK);
    if (ret != 0) goto out_fd;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)host, strlen(host));
    if (ret != 0) goto out_tls;
    ret = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) goto out_tls;
#ifdef HTTPS_TEST_LOCAL
    mbedtls_x509_crt_init(&ca);
    ca_initialized = 1;
    ret = load_test_ca(&ca);
    if (ret != 0) goto out_tls;
    mbedtls_ssl_conf_ca_chain(&config, &ca, NULL);
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
#else
    /* External HTTPS currently has no bundled public-root trust store. */
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_NONE);
#endif
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &drbg);
    ret = mbedtls_ssl_setup(&ssl, &config);
    if (ret != 0) goto out_tls;
    ret = mbedtls_ssl_set_hostname(&ssl, host);
    if (ret != 0) goto out_tls;
    mbedtls_ssl_set_bio(&ssl, &fd, tls_send, tls_recv, NULL);
    ret = tls_handshake(&ssl, fd);
    if (ret != 0) goto out_tls;
    snprintf((char *)request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
             path, host);
    ret = tls_write_all(&ssl, fd, request, strlen((char *)request));
    if (ret != 0) goto out_tls;
    for (;;) {
        ret = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
        if (ret > 0) {
            write(1, buffer, (size_t)ret);
            if (!response_started) {
                size_t copy = (size_t)ret < sizeof(request) - 1 - header_length ?
                               (size_t)ret : sizeof(request) - 1 - header_length;
                memcpy(request + header_length, buffer, copy);
                header_length += copy;
                request[header_length] = 0;
                {
                    char *marker = strstr((char *)request, "\r\n\r\n");
                    if (marker != NULL) {
                        char *length_marker = strstr((char *)request,
                                                     "Content-Length:");
                        response_started = 1;
                        if (length_marker != NULL) {
                            length_marker += strlen("Content-Length:");
                            while (*length_marker == ' ' ||
                                   *length_marker == '\t') {
                                length_marker++;
                            }
                            body_expected = 0;
                            while (*length_marker >= '0' &&
                                   *length_marker <= '9') {
                                body_expected = body_expected * 10 +
                                    *length_marker++ - '0';
                            }
                        }
                        {
                            size_t header_bytes = (size_t)(marker + 4 -
                                                            (char *)request);
                            if ((size_t)ret > header_bytes) {
                                body_received = (size_t)ret - header_bytes;
                            }
                        }
                    }
                }
            }
            else {
                body_received += (size_t)ret;
            }
            if (body_expected >= 0 && body_received >=
                (size_t)body_expected) {
                ret = 0;
                break;
            }
            continue;
        }
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            ret = 0;
            break;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            if (tls_wait(fd, POLLIN) != 0) { ret = -E_TIMEOUT; break; }
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (tls_wait(fd, POLLOUT) != 0) { ret = -E_TIMEOUT; break; }
            continue;
        }
        break;
    }
out_tls:
    if (ca_initialized) {
        mbedtls_x509_crt_free(&ca);
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
out_fd:
    close(fd);
    return ret;
}

int
main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "example.com";
    const char *path = argc > 2 ? argv[2] : "/";
    int ret = https_fetch(host, path);
    if (ret != 0) {
        cprintf("httpsget failed: %d\n", ret);
        return 1;
    }
    cprintf("HTTPS TLS client test pass.\n");
    return 0;
}
