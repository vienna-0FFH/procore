#ifndef __KERN_DRIVER_PCI_CONFIG_H__
#define __KERN_DRIVER_PCI_CONFIG_H__

/* Platform policy knobs for the legacy x86 PCI configuration mechanism. */
#ifndef PCI_MAX_DEVICES
#define PCI_MAX_DEVICES              64
#endif

#ifndef PCI_MAX_BUS
#define PCI_MAX_BUS                  1
#endif

#ifndef PCI_MMIO_BASE
#define PCI_MMIO_BASE                0xF9000000U
#endif

#ifndef PCI_MMIO_LIMIT
#define PCI_MMIO_LIMIT               0xFAC00000U
#endif

#if PCI_MAX_DEVICES < 1
#error "PCI_MAX_DEVICES must be positive"
#endif

#if PCI_MAX_BUS < 1 || PCI_MAX_BUS > 256
#error "PCI_MAX_BUS must be in the range 1..256"
#endif

#if PCI_MMIO_BASE >= PCI_MMIO_LIMIT || (PCI_MMIO_BASE & 0xFFF) != 0 || \
    (PCI_MMIO_LIMIT & 0xFFF) != 0
#error "PCI MMIO window must be page aligned and non-empty"
#endif

#endif /* !__KERN_DRIVER_PCI_CONFIG_H__ */
