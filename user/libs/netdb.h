#ifndef __USER_LIBS_NETDB_H__
#define __USER_LIBS_NETDB_H__

#include <defs.h>

/* Resolve one IPv4 A record through the configured QEMU/user-net DNS proxy. */
int dns_resolve_ipv4(const char *hostname, uint32_t *address_store);

#endif /* !__USER_LIBS_NETDB_H__ */
