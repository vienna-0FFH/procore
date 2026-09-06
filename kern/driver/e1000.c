#include <defs.h>
#include <e1000.h>
#include <e1000_config.h>
#include <clock.h>
#include <error.h>
#include <kmalloc.h>
#include <mmu.h>
#include <pci.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <x86.h>

/* Register offsets for the legacy descriptor format. */
#define E1000_REG_CTRL               0x0000
#define E1000_REG_STATUS             0x0008
#define E1000_REG_ICR                0x00C0
#define E1000_REG_IMS                0x00D0
#define E1000_REG_IMC                0x00D8
#define E1000_REG_RCTL               0x0100
#define E1000_REG_TCTL               0x0400
#define E1000_REG_TIPG               0x0410
#define E1000_REG_RDBAL              0x2800
#define E1000_REG_RDBAH              0x2804
#define E1000_REG_RDLEN              0x2808
#define E1000_REG_RDH               0x2810
#define E1000_REG_RDT               0x2818
#define E1000_REG_TDBAL              0x3800
#define E1000_REG_TDBAH              0x3804
#define E1000_REG_TDLEN              0x3808
#define E1000_REG_TDH               0x3810
#define E1000_REG_TDT               0x3818
#define E1000_REG_RAL               0x5400
#define E1000_REG_RAH               0x5404
#define E1000_REG_MDIC              0x0020

#define E1000_CTRL_SLU              (1U << 6)
#define E1000_CTRL_RST              (1U << 26)
#define E1000_STATUS_LU             (1U << 1)

#define E1000_RCTL_EN               (1U << 1)
#define E1000_RCTL_UPE              (1U << 3)
#define E1000_RCTL_MPE              (1U << 4)
#define E1000_RCTL_BAM              (1U << 15)
#define E1000_RCTL_SZ_2048          (0U << 16)
#define E1000_RCTL_SECRC            (1U << 26)

#define E1000_TCTL_EN               (1U << 1)
#define E1000_TCTL_PSP              (1U << 3)
#define E1000_TCTL_CT               (0x10U << 4)
#define E1000_TCTL_COLD             (0x40U << 12)

#define E1000_TIPG_DEFAULT          ((10U << 0) | (10U << 10) | (10U << 20))

#define E1000_MDIC_PHY              (1U << 21)
#define E1000_MDIC_OP_READ          (2U << 26)
#define E1000_MDIC_OP_WRITE         (1U << 26)
#define E1000_MDIC_READY            (1U << 28)
#define E1000_MDIC_ERROR            (1U << 30)
#define E1000_MDIC_DATA_MASK        0xFFFFU
#define E1000_MDIC_REG_SHIFT        16

#define E1000_PHY_BMCR              0
#define E1000_PHY_LOOPBACK          (1U << 14)
#define E1000_PHY_SPEED_1000        (1U << 6)
#define E1000_PHY_FULL_DUPLEX       (1U << 8)

#define E1000_RXD_STAT_DD            (1U << 0)
#define E1000_RXD_STAT_EOP           (1U << 1)
#define E1000_TXD_CMD_EOP            (1U << 0)
#define E1000_TXD_CMD_IFCS           (1U << 1)
#define E1000_TXD_CMD_RS             (1U << 3)
#define E1000_TXD_STAT_DD            (1U << 0)

struct e1000_rx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

struct e1000_rx_frame {
    uint16_t length;
    uint8_t data[E1000_RX_BUFFER_SIZE];
};

static struct {
    const struct pci_device *pci;
    volatile uint8_t *mmio;
    volatile struct e1000_rx_desc *rx_ring;
    volatile struct e1000_tx_desc *tx_ring;
    struct Page *rx_ring_page;
    struct Page *tx_ring_page;
    struct Page *rx_pages[E1000_RX_RING_LEN];
    struct Page *tx_page;
    uint8_t *tx_buffer;
    uint32_t rx_index;
    uint32_t tx_index;
    struct e1000_rx_frame rx_queue[E1000_RX_QUEUE_LEN];
    uint32_t rx_queue_head;
    uint32_t rx_queue_tail;
    uint32_t rx_queue_count;
    uint8_t mac[6];
    spinlock_t lock;
    struct e1000_stats stats;
    bool ready;
} e1000;

static void e1000_free_rings(void);

static inline uint32_t
e1000_read(uint32_t offset) {
    return *(volatile uint32_t *)(e1000.mmio + offset);
}

static inline void
e1000_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(e1000.mmio + offset) = value;
}

static void
e1000_stop(void) {
    if (e1000.mmio != NULL) {
        e1000_write(E1000_REG_RCTL, 0);
        e1000_write(E1000_REG_TCTL, 0);
        (void)e1000_read(E1000_REG_STATUS);
    }
}

static inline uint32_t
e1000_phys(const void *address) {
    return (uint32_t)PADDR(address);
}

static void
e1000_delay(uint32_t loops) {
    while (loops-- != 0) {
        asm volatile ("pause");
    }
}

static int
e1000_wait_reg(uint32_t offset, uint32_t mask, uint32_t expected,
               uint32_t loops) {
    while (loops-- != 0) {
        if ((e1000_read(offset) & mask) == expected) {
            return 0;
        }
        asm volatile ("pause");
    }
    return -E_TIMEOUT;
}

static int
e1000_phy_write(uint8_t reg, uint16_t value) {
    uint32_t command = E1000_MDIC_PHY |
                       ((uint32_t)reg << E1000_MDIC_REG_SHIFT) |
                       E1000_MDIC_OP_WRITE | value;
    uint32_t result;
    e1000_write(E1000_REG_MDIC, command);
    if (e1000_wait_reg(E1000_REG_MDIC, E1000_MDIC_READY,
                       E1000_MDIC_READY, E1000_RESET_WAIT_LOOPS) != 0) {
        return -E_TIMEOUT;
    }
    result = e1000_read(E1000_REG_MDIC);
    return (result & E1000_MDIC_ERROR) != 0 ? -E_UNSPECIFIED : 0;
}

static int
e1000_reset(void) {
    uint32_t control = e1000_read(E1000_REG_CTRL);
    e1000_write(E1000_REG_IMC, 0xFFFFFFFFU);
    (void)e1000_read(E1000_REG_ICR);
    e1000_write(E1000_REG_CTRL, control | E1000_CTRL_RST);
    e1000_delay(E1000_RESET_WAIT_LOOPS / 16U + 1U);
    if (e1000_wait_reg(E1000_REG_CTRL, E1000_CTRL_RST, 0,
                       E1000_RESET_WAIT_LOOPS) != 0) {
        return -E_TIMEOUT;
    }
    e1000_write(E1000_REG_CTRL, E1000_CTRL_SLU);
    e1000_write(E1000_REG_IMC, 0xFFFFFFFFU);
    (void)e1000_read(E1000_REG_ICR);
    return 0;
}

static int
e1000_alloc_rings(void) {
    size_t i;
    e1000.rx_ring_page = alloc_page();
    e1000.tx_ring_page = alloc_page();
    e1000.tx_page = alloc_page();
    if (e1000.rx_ring_page == NULL || e1000.tx_ring_page == NULL ||
        e1000.tx_page == NULL) {
        e1000_free_rings();
        return -E_NO_MEM;
    }
    e1000.rx_ring = page2kva(e1000.rx_ring_page);
    e1000.tx_ring = page2kva(e1000.tx_ring_page);
    e1000.tx_buffer = page2kva(e1000.tx_page);
    memset((void *)e1000.rx_ring, 0, PGSIZE);
    memset((void *)e1000.tx_ring, 0, PGSIZE);
    memset(e1000.tx_buffer, 0, PGSIZE);
    for (i = 0; i < E1000_RX_RING_LEN; i++) {
        e1000.rx_pages[i] = alloc_page();
        if (e1000.rx_pages[i] == NULL) {
            e1000_free_rings();
            return -E_NO_MEM;
        }
        e1000.rx_ring[i].buffer_addr = page2pa(e1000.rx_pages[i]);
        e1000.rx_ring[i].status = 0;
    }
    e1000.rx_index = 0;
    e1000.tx_index = 0;
    e1000.rx_queue_head = 0;
    e1000.rx_queue_tail = 0;
    e1000.rx_queue_count = 0;
    return 0;
}

static void
e1000_free_rings(void) {
    size_t i;
    for (i = 0; i < E1000_RX_RING_LEN; i++) {
        if (e1000.rx_pages[i] != NULL) {
            free_page(e1000.rx_pages[i]);
            e1000.rx_pages[i] = NULL;
        }
    }
    if (e1000.tx_page != NULL) {
        free_page(e1000.tx_page);
        e1000.tx_page = NULL;
    }
    if (e1000.rx_ring_page != NULL) {
        free_page(e1000.rx_ring_page);
        e1000.rx_ring_page = NULL;
    }
    if (e1000.tx_ring_page != NULL) {
        free_page(e1000.tx_ring_page);
        e1000.tx_ring_page = NULL;
    }
    e1000.rx_ring = NULL;
    e1000.tx_ring = NULL;
    e1000.tx_buffer = NULL;
}

static void
e1000_program_rings(void) {
    uint32_t i;
    e1000_write(E1000_REG_RDBAL,
                e1000_phys((const void *)(uintptr_t)e1000.rx_ring));
    e1000_write(E1000_REG_RDBAH, 0);
    e1000_write(E1000_REG_RDLEN,
                E1000_RX_RING_LEN * sizeof(struct e1000_rx_desc));
    e1000_write(E1000_REG_RDH, 0);
    e1000_write(E1000_REG_RDT, E1000_RX_RING_LEN - 1);

    e1000_write(E1000_REG_TDBAL,
                e1000_phys((const void *)(uintptr_t)e1000.tx_ring));
    e1000_write(E1000_REG_TDBAH, 0);
    e1000_write(E1000_REG_TDLEN,
                E1000_TX_RING_LEN * sizeof(struct e1000_tx_desc));
    e1000_write(E1000_REG_TDH, 0);
    e1000_write(E1000_REG_TDT, 0);
    for (i = 0; i < E1000_TX_RING_LEN; i++) {
        e1000.tx_ring[i].status = E1000_TXD_STAT_DD;
    }

    e1000_write(E1000_REG_TIPG, E1000_TIPG_DEFAULT);
    e1000_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                E1000_TCTL_CT | E1000_TCTL_COLD);
    e1000_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE |
                E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 |
                E1000_RCTL_SECRC);
}

static void
e1000_read_mac(void) {
    uint32_t low = e1000_read(E1000_REG_RAL);
    uint32_t high = e1000_read(E1000_REG_RAH);
    e1000.mac[0] = (uint8_t)low;
    e1000.mac[1] = (uint8_t)(low >> 8);
    e1000.mac[2] = (uint8_t)(low >> 16);
    e1000.mac[3] = (uint8_t)(low >> 24);
    e1000.mac[4] = (uint8_t)high;
    e1000.mac[5] = (uint8_t)(high >> 8);
}

static int
e1000_selftest(void) {
    volatile struct e1000_tx_desc *desc;
    uint32_t slot = e1000.tx_index;
    uint32_t next = (slot + 1) & (E1000_TX_RING_LEN - 1);
    uint32_t i;

    /* Use a valid Ethernet destination/source pair.  Some e1000 revisions
     * apply the receive address filter even while PHY loopback is enabled. */
    memcpy(e1000.tx_buffer, e1000.mac, 6);
    memcpy(e1000.tx_buffer + 6, e1000.mac, 6);
    e1000.tx_buffer[12] = 0x08;
    e1000.tx_buffer[13] = 0x00;
    for (i = 14; i < E1000_SELFTEST_LENGTH; i++) {
        e1000.tx_buffer[i] = (uint8_t)(0xA0U + i);
    }
    desc = &e1000.tx_ring[slot];
    desc->buffer_addr = page2pa(e1000.tx_page);
    desc->length = E1000_SELFTEST_LENGTH;
    desc->checksum_offset = 0;
    desc->command = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
                    E1000_TXD_CMD_RS;
    desc->checksum_start = 0;
    desc->special = 0;
    desc->status = 0;
    barrier();
    e1000_write(E1000_REG_TDT, next);
    for (i = 0; i < E1000_TX_WAIT_LOOPS; i++) {
        if ((desc->status & E1000_TXD_STAT_DD) != 0) {
            e1000.tx_index = next;
            e1000.stats.tx_packets++;
            return 0;
        }
        asm volatile ("pause");
    }
    e1000.stats.tx_errors++;
    cprintf("e1000: TX timeout ctrl=%08x status=%08x tctl=%08x "
            "tdbal=%08x tdlen=%08x tdh=%08x tdt=%08x desc=%02x\n",
            e1000_read(E1000_REG_CTRL), e1000_read(E1000_REG_STATUS),
            e1000_read(E1000_REG_TCTL), e1000_read(E1000_REG_TDBAL),
            e1000_read(E1000_REG_TDLEN), e1000_read(E1000_REG_TDH),
            e1000_read(E1000_REG_TDT), desc->status);
    return -E_TIMEOUT;
}

void
e1000_init(void) {
    const struct pci_device *device;
    int ret;

    memset(&e1000, 0, sizeof(e1000));
    spin_init(&e1000.lock);
    device = pci_find(E1000_VENDOR_ID, E1000_DEVICE_ID, 0);
    if (device == NULL) {
        cprintf("e1000: no supported controller (loopback only)\n");
        return;
    }
    e1000.pci = device;
    ret = pci_enable_device(device);
    if (ret != 0) {
        cprintf("e1000: PCI enable failed (%d)\n", ret);
        return;
    }
    e1000.mmio = pci_iomap(device, 0, E1000_MMIO_SIZE);
    if (e1000.mmio == NULL) {
        cprintf("e1000: BAR0 MMIO mapping failed (bar0=%08x bar1=%08x)\n",
                device->bar[0], device->bar[1]);
        return;
    }
    if (e1000_reset() != 0 || e1000_alloc_rings() != 0) {
        cprintf("e1000: reset/ring allocation failed\n");
        e1000.stats.present = 0;
        e1000_stop();
        e1000_free_rings();
        return;
    }
    e1000_read_mac();
    e1000_program_rings();
    e1000.stats.present = 1;
    e1000.stats.link_up = (e1000_read(E1000_REG_STATUS) & E1000_STATUS_LU) != 0;
    e1000.ready = 1;
    /* QEMU and real 8254x parts expose a PHY loopback bit through MDIC.  It
     * makes the probe self-test independent of an external switch or a host
     * network, while leaving the link in normal mode afterwards. */
    if (e1000_phy_write(E1000_PHY_BMCR,
                        E1000_PHY_SPEED_1000 | E1000_PHY_FULL_DUPLEX |
                        E1000_PHY_LOOPBACK) != 0) {
        e1000.ready = 0;
        e1000.stats.present = 0;
        e1000_stop();
        e1000_free_rings();
        cprintf("e1000: PHY loopback setup failed\n");
        return;
    }
    ret = e1000_selftest();
    /* TX DMA completion is the probe criterion.  Leave PHY loopback before
     * exposing the device to the network stack; otherwise an early ARP
     * request can be reflected locally and never reach the QEMU backend. */
    (void)e1000_phy_write(E1000_PHY_BMCR,
                          E1000_PHY_SPEED_1000 | E1000_PHY_FULL_DUPLEX);
    if (ret != 0) {
        e1000.ready = 0;
        e1000.stats.present = 0;
        e1000_stop();
        e1000_free_rings();
        cprintf("e1000: controller at %02x:%02x.%u self-test failed (%d)\n",
                device->bus, device->slot, device->function, ret);
        return;
    }
    cprintf("e1000: %02x:%02x.%u ready, MAC %02x:%02x:%02x:%02x:%02x:%02x, link %s\n",
            device->bus, device->slot, device->function,
            e1000.mac[0], e1000.mac[1], e1000.mac[2], e1000.mac[3],
            e1000.mac[4], e1000.mac[5], e1000.stats.link_up ? "up" : "down");
}

void
e1000_poll(void) {
    volatile struct e1000_rx_desc *desc;
    uint32_t old_index;
    bool intr_flag;
    if (!e1000.ready) {
        return;
    }
    local_intr_save(intr_flag);
    spin_lock(&e1000.lock);
    /* Move completed descriptors into a bounded software queue.  A single
     * legacy descriptor is enough for the configured MTU; fragmented frames
     * are rejected and every descriptor is returned to hardware. */
    for (;;) {
        desc = &e1000.rx_ring[e1000.rx_index];
        if ((desc->status & E1000_RXD_STAT_DD) == 0) {
            break;
        }
        if ((desc->status & E1000_RXD_STAT_EOP) != 0 &&
            desc->length != 0 && desc->length <= E1000_RX_BUFFER_SIZE) {
            if (e1000.rx_queue_count < E1000_RX_QUEUE_LEN) {
                e1000.rx_queue[e1000.rx_queue_tail].length = desc->length;
                memcpy(e1000.rx_queue[e1000.rx_queue_tail].data,
                       page2kva(e1000.rx_pages[e1000.rx_index]), desc->length);
                e1000.rx_queue_tail =
                    (e1000.rx_queue_tail + 1) % E1000_RX_QUEUE_LEN;
                e1000.rx_queue_count++;
                e1000.stats.rx_packets++;
            }
            else {
                e1000.stats.rx_errors++;
            }
        }
        else {
            e1000.stats.rx_errors++;
        }
        desc->status = 0;
        desc->length = 0;
        old_index = e1000.rx_index;
        e1000.rx_index = (old_index + 1) & (E1000_RX_RING_LEN - 1);
        /* RDT names the last descriptor returned to hardware. */
        e1000_write(E1000_REG_RDT, old_index);
    }
    spin_unlock(&e1000.lock);
    local_intr_restore(intr_flag);
}

int
e1000_receive(void *data, size_t capacity) {
    struct e1000_rx_frame *frame;
    size_t length;
    bool intr_flag;

    if (!e1000.ready || data == NULL || capacity == 0) {
        return -E_INVAL;
    }
    /* Poll once here so consumers are not tied to timer frequency. */
    e1000_poll();
    local_intr_save(intr_flag);
    spin_lock(&e1000.lock);
    if (e1000.rx_queue_count == 0) {
        spin_unlock(&e1000.lock);
        local_intr_restore(intr_flag);
        return 0;
    }
    frame = &e1000.rx_queue[e1000.rx_queue_head];
    length = frame->length;
    if (length > capacity) {
        /* Consume an oversized frame so one hostile/jumbo packet cannot
         * permanently pin the software queue. */
        e1000.rx_queue_head =
            (e1000.rx_queue_head + 1) % E1000_RX_QUEUE_LEN;
        e1000.rx_queue_count--;
        e1000.stats.rx_errors++;
        spin_unlock(&e1000.lock);
        local_intr_restore(intr_flag);
        return -E_TOO_BIG;
    }
    memcpy(data, frame->data, length);
    e1000.rx_queue_head =
        (e1000.rx_queue_head + 1) % E1000_RX_QUEUE_LEN;
    e1000.rx_queue_count--;
    spin_unlock(&e1000.lock);
    local_intr_restore(intr_flag);
    return (int)length;
}

bool
e1000_present(void) {
    return e1000.stats.present != 0;
}

void
e1000_get_mac(uint8_t mac[6]) {
    bool intr_flag;
    if (mac == NULL) {
        return;
    }
    local_intr_save(intr_flag);
    spin_lock(&e1000.lock);
    memcpy(mac, e1000.mac, sizeof(e1000.mac));
    spin_unlock(&e1000.lock);
    local_intr_restore(intr_flag);
}

int
e1000_transmit(const void *data, size_t length) {
    volatile struct e1000_tx_desc *desc;
    uint32_t slot;
    uint32_t next;
    uint32_t i;
    bool intr_flag;
    if (!e1000.ready || data == NULL || length == 0 ||
        length > E1000_TX_BUFFER_SIZE) {
        return -E_INVAL;
    }
    local_intr_save(intr_flag);
    spin_lock(&e1000.lock);
    slot = e1000.tx_index;
    next = (slot + 1) & (E1000_TX_RING_LEN - 1);
    desc = &e1000.tx_ring[slot];
    if ((desc->status & E1000_TXD_STAT_DD) == 0) {
        spin_unlock(&e1000.lock);
        local_intr_restore(intr_flag);
        return -E_BUSY;
    }
    memcpy(e1000.tx_buffer, data, length);
    desc->buffer_addr = page2pa(e1000.tx_page);
    desc->length = (uint16_t)length;
    desc->checksum_offset = 0;
    desc->command = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
                    E1000_TXD_CMD_RS;
    desc->checksum_start = 0;
    desc->special = 0;
    desc->status = 0;
    barrier();
    e1000_write(E1000_REG_TDT, next);
    for (i = 0; i < E1000_TX_WAIT_LOOPS; i++) {
        if ((desc->status & E1000_TXD_STAT_DD) != 0) {
            e1000.tx_index = next;
            e1000.stats.tx_packets++;
            spin_unlock(&e1000.lock);
            local_intr_restore(intr_flag);
            return (int)length;
        }
        asm volatile ("pause");
    }
    e1000.stats.tx_errors++;
    spin_unlock(&e1000.lock);
    local_intr_restore(intr_flag);
    return -E_TIMEOUT;
}

void
e1000_get_stats(struct e1000_stats *stats) {
    bool intr_flag;
    if (stats == NULL) {
        return;
    }
    local_intr_save(intr_flag);
    spin_lock(&e1000.lock);
    *stats = e1000.stats;
    spin_unlock(&e1000.lock);
    local_intr_restore(intr_flag);
}
