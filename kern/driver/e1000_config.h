#ifndef __KERN_DRIVER_E1000_CONFIG_H__
#define __KERN_DRIVER_E1000_CONFIG_H__

/* The legacy 82540/82545 programming model used by QEMU's e1000 device. */
#ifndef E1000_VENDOR_ID
#define E1000_VENDOR_ID               0x8086
#endif

#ifndef E1000_DEVICE_ID
#define E1000_DEVICE_ID               0x100E
#endif

#ifndef E1000_MMIO_SIZE
#define E1000_MMIO_SIZE               0x20000U
#endif

#ifndef E1000_RX_RING_LEN
#define E1000_RX_RING_LEN             8U
#endif

#ifndef E1000_TX_RING_LEN
#define E1000_TX_RING_LEN             8U
#endif

#ifndef E1000_RX_BUFFER_SIZE
#define E1000_RX_BUFFER_SIZE          2048U
#endif

#ifndef E1000_TX_BUFFER_SIZE
#define E1000_TX_BUFFER_SIZE          2048U
#endif

#ifndef E1000_RX_QUEUE_LEN
#define E1000_RX_QUEUE_LEN             8U
#endif

#ifndef E1000_RESET_WAIT_LOOPS
#define E1000_RESET_WAIT_LOOPS        100000U
#endif

#ifndef E1000_TX_WAIT_LOOPS
#define E1000_TX_WAIT_LOOPS           1000000U
#endif

#ifndef E1000_SELFTEST_LENGTH
#define E1000_SELFTEST_LENGTH         60U
#endif


#if E1000_RX_RING_LEN < 2 || E1000_RX_RING_LEN > 256 || \
    (E1000_RX_RING_LEN & (E1000_RX_RING_LEN - 1)) != 0
#error "E1000_RX_RING_LEN must be a power of two in the range 2..256"
#endif

#if E1000_TX_RING_LEN < 2 || E1000_TX_RING_LEN > 256 || \
    (E1000_TX_RING_LEN & (E1000_TX_RING_LEN - 1)) != 0
#error "E1000_TX_RING_LEN must be a power of two in the range 2..256"
#endif

#if E1000_RX_BUFFER_SIZE < 2048 || E1000_RX_BUFFER_SIZE > 4096 || \
    (E1000_RX_BUFFER_SIZE & (E1000_RX_BUFFER_SIZE - 1)) != 0
#error "E1000_RX_BUFFER_SIZE must be a power of two in the range 2048..4096"
#endif

#if E1000_TX_BUFFER_SIZE < 64 || E1000_TX_BUFFER_SIZE > 4096
#error "E1000_TX_BUFFER_SIZE must be in the range 64..4096"
#endif

#if E1000_RX_QUEUE_LEN < 1 || E1000_RX_QUEUE_LEN > 256
#error "E1000_RX_QUEUE_LEN must be in the range 1..256"
#endif

#endif /* !__KERN_DRIVER_E1000_CONFIG_H__ */
