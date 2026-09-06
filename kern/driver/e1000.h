#ifndef __KERN_DRIVER_E1000_H__
#define __KERN_DRIVER_E1000_H__

#include <defs.h>

struct e1000_stats {
    uint32_t present;
    uint32_t link_up;
    uint32_t tx_packets;
    uint32_t tx_errors;
    uint32_t rx_packets;
    uint32_t rx_errors;
};

void e1000_init(void);
void e1000_poll(void);
bool e1000_present(void);
int e1000_transmit(const void *data, size_t length);
/* Copy one complete received Ethernet frame into @data.  Return zero when
 * the ring is empty, a negative error for a malformed frame, or the frame
 * length on success. */
int e1000_receive(void *data, size_t capacity);
void e1000_get_stats(struct e1000_stats *stats);

#endif /* !__KERN_DRIVER_E1000_H__ */
