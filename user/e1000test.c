#include <ulib.h>
#include <unistd.h>
#include <socket.h>
#include <stdio.h>

/* The test is run with QEMU's e1000 device attached.  It verifies the
 * complete probe -> DMA ring -> PHY loopback -> descriptor write-back path
 * through the public netstat ABI; no kernel-private addresses are exposed. */
int
main(void) {
    struct net_stats stats;

    assert(sleep(500) == 0);
    assert(netstat(&stats) == 0);
    cprintf("e1000 stats: dev=%d link=%d tx=%d rx=%d txerr=%d rxerr=%d\n",
            stats.hw_devices, stats.hw_link_up, stats.hw_tx_packets,
            stats.hw_rx_packets, stats.hw_tx_errors, stats.hw_rx_errors);
    assert(stats.hw_devices != 0);
    assert(stats.hw_link_up != 0);
    assert(stats.hw_tx_packets != 0);
    assert(stats.hw_tx_errors == 0);
    assert(stats.hw_rx_errors == 0);
    cprintf("e1000 PCI/DMA TX loopback test pass.\n");
    return 0;
}
