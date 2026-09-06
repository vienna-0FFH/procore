#include <defs.h>
#include <error.h>
#include <mmu.h>
#include <memlayout.h>
#include <pmm.h>
#include <pci.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <x86.h>

static struct pci_device pci_devices[PCI_MAX_DEVICES];
static int pci_devices_used;
static uintptr_t pci_next_mmio = PCI_MMIO_BASE;
static spinlock_t pci_config_lock;

static uint32_t
pci_config_address(uint8_t bus, uint8_t slot, uint8_t function,
                   uint8_t offset) {
    return 0x80000000U |
           ((uint32_t)bus << 16) |
           ((uint32_t)(slot & 0x1F) << 11) |
           ((uint32_t)(function & 0x07) << 8) |
           (offset & 0xFC);
}

uint32_t
pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                  uint8_t offset) {
    uint32_t value;
    bool intr_flag;
    local_intr_save(intr_flag);
    spin_lock(&pci_config_lock);
    outl(PCI_CONFIG_ADDRESS,
         pci_config_address(bus, slot, function, offset));
    value = inl(PCI_CONFIG_DATA);
    spin_unlock(&pci_config_lock);
    local_intr_restore(intr_flag);
    return value;
}

void
pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                   uint8_t offset, uint32_t value) {
    bool intr_flag;
    local_intr_save(intr_flag);
    spin_lock(&pci_config_lock);
    outl(PCI_CONFIG_ADDRESS,
         pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
    spin_unlock(&pci_config_lock);
    local_intr_restore(intr_flag);
}

static void
pci_read_device(struct pci_device *device, uint8_t bus, uint8_t slot,
                uint8_t function) {
    uint32_t id = pci_config_read32(bus, slot, function, 0x00);
    uint32_t class_reg = pci_config_read32(bus, slot, function, 0x08);
    uint32_t header_reg = pci_config_read32(bus, slot, function, 0x0C);
    uint32_t irq_reg = pci_config_read32(bus, slot, function,
                                         PCI_INTERRUPT_LINE & ~3);
    int i;

    memset(device, 0, sizeof(*device));
    device->bus = bus;
    device->slot = slot;
    device->function = function;
    device->vendor_id = (uint16_t)(id & 0xFFFF);
    device->device_id = (uint16_t)(id >> 16);
    device->revision = (uint8_t)(class_reg & 0xFF);
    device->prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
    device->subclass = (uint8_t)((class_reg >> 16) & 0xFF);
    device->class_code = (uint8_t)(class_reg >> 24);
    device->header_type = (uint8_t)((header_reg >> 16) & 0xFF);
    device->irq_line = (uint8_t)(irq_reg & 0xFF);
    for (i = 0; i < PCI_BAR_COUNT; i++) {
        device->bar[i] = pci_config_read32(bus, slot, function,
                                           (uint8_t)(PCI_BAR0 + i * 4));
    }
}

static bool
pci_function_present(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t id = pci_config_read32(bus, slot, function, 0);
    return (id & 0xFFFFU) != PCI_VENDOR_INVALID;
}

void
pci_init(void) {
    uint32_t bus;
    uint32_t slot;

    pci_devices_used = 0;
    pci_next_mmio = PCI_MMIO_BASE;
    spin_init(&pci_config_lock);
    for (bus = 0; bus < PCI_MAX_BUS && pci_devices_used < PCI_MAX_DEVICES;
         bus++) {
        for (slot = 0; slot < 32 && pci_devices_used < PCI_MAX_DEVICES;
             slot++) {
            uint8_t function;
            uint32_t header;
            if (!pci_function_present((uint8_t)bus, (uint8_t)slot, 0)) {
                continue;
            }
            header = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0,
                                       PCI_HEADER_TYPE & ~3);
            for (function = 0; function < 8 &&
                 pci_devices_used < PCI_MAX_DEVICES; function++) {
                struct pci_device *device;
                if (function != 0 && !(header & (1U << 23))) {
                    break;
                }
                if (!pci_function_present((uint8_t)bus, (uint8_t)slot,
                                          function)) {
                    continue;
                }
                device = &pci_devices[pci_devices_used++];
                pci_read_device(device, (uint8_t)bus, (uint8_t)slot,
                                function);
                cprintf("pci: %02x:%02x.%u vendor=%04x device=%04x class=%02x:%02x irq=%u\n",
                        device->bus, device->slot, device->function,
                        device->vendor_id, device->device_id,
                        device->class_code, device->subclass,
                        device->irq_line);
            }
        }
    }
    cprintf("pci: %d device(s) discovered\n", pci_devices_used);
}

int
pci_bar_size(const struct pci_device *device, int bar_index,
             size_t *size_store) {
    uint32_t original_low;
    uint32_t original_high = 0;
    uint32_t mask_low;
    uint32_t mask_high = 0;
    uint64_t size;
    uint8_t offset;
    uint32_t command;

    if (device == NULL || size_store == NULL || bar_index < 0 ||
        bar_index >= PCI_BAR_COUNT) {
        return -E_INVAL;
    }
    original_low = device->bar[bar_index];
    if (original_low == 0) {
        *size_store = 0;
        return -E_NA_DEV;
    }
    offset = (uint8_t)(PCI_BAR0 + bar_index * 4);
    command = pci_config_read32(device->bus, device->slot,
                                device->function, PCI_COMMAND & ~3);
    /* PCI requires memory/I/O decoding to be disabled while a BAR is probed. */
    pci_config_write32(device->bus, device->slot, device->function,
                       PCI_COMMAND & ~3,
                       command & ~(PCI_COMMAND_MEMORY | 0x0001U));
    if ((original_low & PCI_BAR_IO) != 0) {
        pci_config_write32(device->bus, device->slot, device->function,
                           offset, 0xFFFFFFFFU);
        mask_low = pci_config_read32(device->bus, device->slot,
                                     device->function, offset);
        pci_config_write32(device->bus, device->slot, device->function,
                           offset, original_low);
        mask_low &= ~0x3U;
        size = (uint64_t)(~mask_low + 1U);
    }
    else {
        if ((original_low & PCI_BAR_MEMORY_64) == PCI_BAR_MEMORY_64 &&
            bar_index + 1 < PCI_BAR_COUNT) {
            original_high = device->bar[bar_index + 1];
        }
        pci_config_write32(device->bus, device->slot, device->function,
                           offset, 0xFFFFFFFFU);
        mask_low = pci_config_read32(device->bus, device->slot,
                                     device->function, offset);
        pci_config_write32(device->bus, device->slot, device->function,
                           offset, original_low);
        if ((original_low & PCI_BAR_MEMORY_64) == PCI_BAR_MEMORY_64 &&
            bar_index + 1 < PCI_BAR_COUNT) {
            pci_config_write32(device->bus, device->slot, device->function,
                               (uint8_t)(offset + 4), 0xFFFFFFFFU);
            mask_high = pci_config_read32(device->bus, device->slot,
                                          device->function,
                                          (uint8_t)(offset + 4));
            pci_config_write32(device->bus, device->slot, device->function,
                               (uint8_t)(offset + 4), original_high);
        }
        mask_low &= PCI_BAR_MEMORY_MASK;
        if ((original_low & PCI_BAR_MEMORY_64) == PCI_BAR_MEMORY_64 &&
            bar_index + 1 < PCI_BAR_COUNT) {
            size = (((uint64_t)mask_high << 32) | mask_low);
            size = ~size + 1;
        }
        else {
            /* Keep the complement in 32 bits for a 32-bit BAR. */
            size = (uint64_t)(~mask_low + 1U);
        }
    }
    pci_config_write32(device->bus, device->slot, device->function,
                       PCI_COMMAND & ~3, command);
    if (size == 0 || size > 0xFFFFFFFFULL) {
        *size_store = 0;
        return -E_TOO_BIG;
    }
    *size_store = (size_t)size;
    return 0;
}

int
pci_device_count(void) {
    return pci_devices_used;
}

const struct pci_device *
pci_device_at(int index) {
    if (index < 0 || index >= pci_devices_used) {
        return NULL;
    }
    return &pci_devices[index];
}

const struct pci_device *
pci_find(uint16_t vendor_id, uint16_t device_id, int occurrence) {
    int i;
    if (occurrence < 0) {
        return NULL;
    }
    for (i = 0; i < pci_devices_used; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            if (occurrence-- == 0) {
                return &pci_devices[i];
            }
        }
    }
    return NULL;
}

int
pci_enable_device(const struct pci_device *device) {
    uint32_t command;
    if (device == NULL) {
        return -E_INVAL;
    }
    command = pci_config_read32(device->bus, device->slot,
                                device->function, PCI_COMMAND & ~3);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write32(device->bus, device->slot, device->function,
                       PCI_COMMAND & ~3, command);
    return 0;
}

static int
pci_bar_memory_base(const struct pci_device *device, int bar_index,
                    uintptr_t *base_store) {
    uint32_t low;
    uint32_t high = 0;
    if (device == NULL || base_store == NULL ||
        bar_index < 0 || bar_index >= PCI_BAR_COUNT) {
        return -E_INVAL;
    }
    low = device->bar[bar_index];
    if (low == 0 || (low & PCI_BAR_IO) != 0) {
        return -E_NA_DEV;
    }
    if ((low & PCI_BAR_MEMORY_64) == PCI_BAR_MEMORY_64 &&
        bar_index + 1 < PCI_BAR_COUNT) {
        high = device->bar[bar_index + 1];
    }
    if (high != 0) {
        return -E_TOO_BIG;
    }
    *base_store = (uintptr_t)(low & PCI_BAR_MEMORY_MASK);
    return 0;
}

void *
pci_iomap(const struct pci_device *device, int bar_index, size_t length) {
    uintptr_t physical;
    uintptr_t map_start;
    uintptr_t map_end;
    uintptr_t virtual_start;
    uintptr_t va;
    size_t bar_size;
    size_t mapped;
    size_t rollback;
    int ret;

    if (length == 0 || length > (uintptr_t)-1 - (PGSIZE - 1)) {
        return NULL;
    }
    ret = pci_bar_memory_base(device, bar_index, &physical);
    if (ret != 0) {
        return NULL;
    }
    if (pci_bar_size(device, bar_index, &bar_size) != 0 ||
        bar_size < length) {
        return NULL;
    }
    if (physical > (uintptr_t)-1 - length) {
        return NULL;
    }
    map_start = ROUNDDOWN(physical, PGSIZE);
    map_end = ROUNDUP(physical + length, PGSIZE);
    if (map_end <= map_start || map_end - map_start >
        PCI_MMIO_LIMIT - PCI_MMIO_BASE) {
        return NULL;
    }
    virtual_start = ROUNDUP(pci_next_mmio, PGSIZE);
    if (virtual_start > PCI_MMIO_LIMIT ||
        map_end - map_start > PCI_MMIO_LIMIT - virtual_start) {
        return NULL;
    }
    mapped = map_end - map_start;
    for (va = 0; va < mapped; va += PGSIZE) {
        pte_t *ptep = get_pte(boot_pgdir, virtual_start + va, 1);
        if (ptep == NULL || (*ptep & PTE_P) != 0) {
            for (rollback = 0; rollback < va; rollback += PGSIZE) {
                pte_t *old = get_pte(boot_pgdir,
                                      virtual_start + rollback, 0);
                if (old != NULL) {
                    *old = 0;
                }
            }
            return NULL;
        }
        *ptep = (map_start + va) | PTE_P | PTE_W | PTE_PWT | PTE_PCD;
        tlb_invalidate(boot_pgdir, virtual_start + va);
    }
    pci_next_mmio = virtual_start + mapped;
    return (void *)(virtual_start + (physical - map_start));
}
