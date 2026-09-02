#include <defs.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <pmm.h>
#include <mmu.h>
#include <vmx.h>

#define CPUID_LEAF_FEATURES             1
#define CPUID_ECX_VMX                  (1U << 5)

static struct vmx_info vmx_cpu;
static struct Page *vmxon_page;
static uint32_t vmxon_pa;
static uint32_t vmxon_old_cr0;
static uint32_t vmxon_old_cr4;

static inline void
vmx_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t result[4]) {
    asm volatile (
        "cpuid"
        : "=a" (result[0]), "=b" (result[1]),
          "=c" (result[2]), "=d" (result[3])
        : "a" (leaf), "c" (subleaf));
}

static inline uint64_t
vmx_rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    return ((uint64_t)high << 32) | low;
}

static inline uint32_t
vmx_read_cr0(void) {
    uint32_t value;
    asm volatile ("movl %%cr0, %0" : "=r" (value) :: "memory");
    return value;
}

static inline void
vmx_write_cr0(uint32_t value) {
    asm volatile ("movl %0, %%cr0" :: "r" (value) : "memory");
}

static inline uint32_t
vmx_read_cr4(void) {
    uint32_t value;
    asm volatile ("movl %%cr4, %0" : "=r" (value) :: "memory");
    return value;
}

static inline void
vmx_write_cr4(uint32_t value) {
    asm volatile ("movl %0, %%cr4" :: "r" (value) : "memory");
}

static uint32_t
vmx_adjust_cr0(uint32_t value) {
    uint64_t fixed0 = vmx_rdmsr(MSR_IA32_VMX_CR0_FIXED0);
    uint64_t fixed1 = vmx_rdmsr(MSR_IA32_VMX_CR0_FIXED1);
    return (value | (uint32_t)fixed0) & (uint32_t)fixed1;
}

static uint32_t
vmx_adjust_cr4(uint32_t value) {
    uint64_t fixed0 = vmx_rdmsr(MSR_IA32_VMX_CR4_FIXED0);
    uint64_t fixed1 = vmx_rdmsr(MSR_IA32_VMX_CR4_FIXED1);
    return (value | (uint32_t)fixed0) & (uint32_t)fixed1;
}

/* VMXON takes a memory operand containing a 64-bit physical address. */
static int
vmx_vmxon(uintptr_t region_pa) {
    uint64_t operand = region_pa;
    uint8_t failed;
    asm volatile (
        ".byte 0xf3, 0x0f, 0xc7, 0x30\n\t"
        "setna %0"
        : "=q" (failed)
        : "a" (&operand), "m" (operand)
        : "cc", "memory");
    return failed ? -E_INVAL : 0;
}

static int
vmx_vmxoff(void) {
    uint8_t failed;
    asm volatile (
        ".byte 0x0f, 0x01, 0xc4\n\t"
        "setna %0"
        : "=q" (failed)
        :
        : "cc", "memory");
    return failed ? -E_INVAL : 0;
}

static int
vmx_vmclear_instruction(uintptr_t vmcs_pa) {
    uint64_t operand = vmcs_pa;
    uint8_t failed;
    asm volatile (
        ".byte 0x66, 0x0f, 0xc7, 0x30\n\t"
        "setna %0"
        : "=q" (failed)
        : "a" (&operand), "m" (operand)
        : "cc", "memory");
    return failed ? -E_INVAL : 0;
}

static int
vmx_vmptrld_instruction(uintptr_t vmcs_pa) {
    uint64_t operand = vmcs_pa;
    uint8_t failed;
    asm volatile (
        ".byte 0x0f, 0xc7, 0x30\n\t"
        "setna %0"
        : "=q" (failed)
        : "a" (&operand), "m" (operand)
        : "cc", "memory");
    return failed ? -E_INVAL : 0;
}

void
vmx_init(void) {
    uint32_t result[4];

    memset(&vmx_cpu, 0, sizeof(vmx_cpu));
    vmx_cpu.status = VMX_STATUS_UNKNOWN;

    vmx_cpuid(0, 0, result);
    vmx_cpu.max_basic_leaf = result[0];
    if (vmx_cpu.max_basic_leaf < CPUID_LEAF_FEATURES) {
        vmx_cpu.status = VMX_STATUS_UNSUPPORTED;
        cprintf("vmx: unavailable (CPUID leaf 1 is missing)\n");
        return;
    }

    vmx_cpuid(CPUID_LEAF_FEATURES, 0, result);
    vmx_cpu.feature_ecx = result[2];
    if ((vmx_cpu.feature_ecx & CPUID_ECX_VMX) == 0) {
        vmx_cpu.status = VMX_STATUS_UNSUPPORTED;
        cprintf("vmx: unavailable (CPUID.1:ECX.VMX is clear)\n");
        return;
    }

    vmx_cpu.basic = vmx_rdmsr(MSR_IA32_VMX_BASIC);
    vmx_cpu.revision = (uint32_t)(vmx_cpu.basic & 0x7fffffffU);
    vmx_cpu.region_size = (uint32_t)((vmx_cpu.basic >> 32) & 0x1fffU);
    vmx_cpu.memory_type = (uint32_t)((vmx_cpu.basic >> 50) & 0xfU);
    vmx_cpu.feature_control = vmx_rdmsr(MSR_IA32_FEATURE_CONTROL);

    if ((vmx_cpu.feature_control & IA32_FEATURE_CONTROL_LOCK) == 0) {
        vmx_cpu.status = VMX_STATUS_FIRMWARE_UNCONFIGURED;
        cprintf("vmx: present but IA32_FEATURE_CONTROL is unlocked\n");
        return;
    }
    if ((vmx_cpu.feature_control & IA32_FEATURE_CONTROL_VMXON) == 0) {
        vmx_cpu.status = VMX_STATUS_LOCKED_OUT;
        cprintf("vmx: present but disabled by IA32_FEATURE_CONTROL\n");
        return;
    }

    vmx_cpu.status = VMX_STATUS_READY;
    cprintf("vmx: ready (revision 0x%08x, VMCS region %u bytes)\n",
            vmx_cpu.revision, vmx_cpu.region_size);
}

const struct vmx_info *
vmx_get_info(void) {
    return &vmx_cpu;
}

bool
vmx_supported(void) {
    return vmx_cpu.status == VMX_STATUS_READY || vmx_cpu.status == VMX_STATUS_ON;
}

bool
vmx_is_on(void) {
    return vmx_cpu.status == VMX_STATUS_ON;
}

int
vmx_enable(void) {
    uint32_t cr0, cr4;
    int ret;

    if (vmx_cpu.status == VMX_STATUS_UNKNOWN) {
        vmx_init();
    }
    if (vmx_cpu.status == VMX_STATUS_ON) {
        return 0;
    }
    if (vmx_cpu.status != VMX_STATUS_READY) {
        return -E_NA_DEV;
    }
    if (vmx_cpu.region_size == 0 || vmx_cpu.region_size > PGSIZE) {
        vmx_cpu.status = VMX_STATUS_FAILED;
        return -E_TOO_BIG;
    }

    if ((vmxon_page = alloc_page()) == NULL) {
        return -E_NO_MEM;
    }
    memset(page2kva(vmxon_page), 0, PGSIZE);
    *(uint32_t *)page2kva(vmxon_page) = vmx_cpu.revision;
    vmxon_pa = (uint32_t)page2pa(vmxon_page);

    cr0 = vmx_read_cr0();
    cr4 = vmx_read_cr4();
    vmxon_old_cr0 = cr0;
    vmxon_old_cr4 = cr4;
    vmx_write_cr0(vmx_adjust_cr0(cr0));
    vmx_write_cr4(vmx_adjust_cr4(cr4) | CR4_VMXE);

    ret = vmx_vmxon(vmxon_pa);
    if (ret != 0) {
        vmx_write_cr0(vmxon_old_cr0);
        vmx_write_cr4(vmxon_old_cr4);
        free_page(vmxon_page);
        vmxon_page = NULL;
        vmx_cpu.status = VMX_STATUS_FAILED;
        return ret;
    }
    vmx_cpu.status = VMX_STATUS_ON;
    return 0;
}

int
vmx_disable(void) {
    int ret = 0;
    if (vmx_cpu.status != VMX_STATUS_ON) {
        return 0;
    }
    ret = vmx_vmxoff();
    vmx_write_cr0(vmxon_old_cr0);
    vmx_write_cr4(vmxon_old_cr4);
    if (vmxon_page != NULL) {
        free_page(vmxon_page);
        vmxon_page = NULL;
    }
    vmxon_pa = 0;
    vmx_cpu.status = (ret == 0) ? VMX_STATUS_READY : VMX_STATUS_FAILED;
    return ret;
}

int
vmx_vmclear(uintptr_t vmcs_pa) {
    if (!vmx_is_on() || (vmcs_pa & (PGSIZE - 1)) != 0) {
        return -E_INVAL;
    }
    return vmx_vmclear_instruction(vmcs_pa);
}

int
vmx_vmptrld(uintptr_t vmcs_pa) {
    if (!vmx_is_on() || (vmcs_pa & (PGSIZE - 1)) != 0) {
        return -E_INVAL;
    }
    return vmx_vmptrld_instruction(vmcs_pa);
}

int
vmx_vmread32(uint32_t field, uint32_t *value_store) {
    uint32_t value;
    uint8_t failed;
    if (!vmx_is_on() || value_store == NULL) {
        return -E_INVAL;
    }
    asm volatile (
        ".byte 0x0f, 0x78, 0xd0\n\t"
        "setna %1"
        : "=a" (value), "=q" (failed)
        : "d" (field)
        : "cc", "memory");
    if (failed) {
        return -E_INVAL;
    }
    *value_store = value;
    return 0;
}

int
vmx_vmwrite32(uint32_t field, uint32_t value) {
    uint8_t failed;
    if (!vmx_is_on()) {
        return -E_INVAL;
    }
    asm volatile (
        ".byte 0x0f, 0x79, 0xd0\n\t"
        "setna %0"
        : "=q" (failed)
        : "a" (value), "d" (field)
        : "cc", "memory");
    return failed ? -E_INVAL : 0;
}
