#ifndef __KERN_DRIVER_PCI_H__
#define __KERN_DRIVER_PCI_H__

#include <defs.h>
#include <pci_config.h>

#define PCI_CONFIG_ADDRESS           0xCF8
#define PCI_CONFIG_DATA              0xCFC

#define PCI_VENDOR_INVALID           0xFFFF
#define PCI_BAR_COUNT                6

#define PCI_COMMAND                  0x04
#define PCI_COMMAND_MEMORY           0x0002
#define PCI_COMMAND_MASTER           0x0004
#define PCI_HEADER_TYPE              0x0E
#define PCI_BAR0                     0x10
#define PCI_INTERRUPT_LINE           0x3C

#define PCI_BAR_IO                   0x1
#define PCI_BAR_MEMORY_64            0x4
#define PCI_BAR_MEMORY_MASK          0xFFFFFFF0U
#define PCI_BAR_PREFETCH             0x8

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t irq_line;
    uint32_t bar[PCI_BAR_COUNT];
};

void pci_init(void);
int pci_device_count(void);
const struct pci_device *pci_device_at(int index);
const struct pci_device *pci_find(uint16_t vendor_id, uint16_t device_id,
                                  int occurrence);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value);
int pci_bar_size(const struct pci_device *device, int bar_index,
                 size_t *size_store);
int pci_enable_device(const struct pci_device *device);
void *pci_iomap(const struct pci_device *device, int bar_index, size_t length);

#endif /* !__KERN_DRIVER_PCI_H__ */
