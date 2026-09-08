#include <netdb.h>
#include <stdio.h>
#include <ulib.h>

int
main(void) {
    uint32_t address;
    assert(dns_resolve_ipv4("example.com", &address) == 0);
    cprintf("DNS A record resolved: %08x\n", address);
    cprintf("dns resolver test pass.\n");
    return 0;
}
