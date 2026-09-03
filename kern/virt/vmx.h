#ifndef __KERN_VIRT_VMX_H__
#define __KERN_VIRT_VMX_H__

#include <defs.h>

/* VMX architectural MSRs used by the first-stage monitor. */
#define MSR_IA32_FEATURE_CONTROL       0x0000003A
#define MSR_IA32_VMX_BASIC             0x00000480
#define MSR_IA32_VMX_CR0_FIXED0        0x00000486
#define MSR_IA32_VMX_CR0_FIXED1        0x00000487
#define MSR_IA32_VMX_CR4_FIXED0        0x00000488
#define MSR_IA32_VMX_CR4_FIXED1        0x00000489

#define IA32_FEATURE_CONTROL_LOCK     (1U << 0)
#define IA32_FEATURE_CONTROL_VMXON    (1U << 2)
#define CR4_VMXE                      (1U << 13)

/* 32-bit guest-state field used by the optional VMCS smoke test. */
#define VMX_VMCS_GUEST_RIP            0x0000681E

enum vmx_status {
    VMX_STATUS_UNKNOWN = 0,
    VMX_STATUS_UNSUPPORTED,
    VMX_STATUS_LOCKED_OUT,
    VMX_STATUS_FIRMWARE_UNCONFIGURED,
    VMX_STATUS_READY,
    VMX_STATUS_ON,
    VMX_STATUS_FAILED,
};

struct vmx_info {
    enum vmx_status status;
    uint32_t max_basic_leaf;
    uint32_t feature_ecx;
    uint64_t feature_control;
    uint64_t basic;
    uint32_t revision;
    uint32_t region_size;
    uint32_t memory_type;
};

void vmx_init(void);
const struct vmx_info *vmx_get_info(void);
bool vmx_supported(void);
bool vmx_is_on(void);

/* These operations are intentionally explicit; vmx_init never enables VMX. */
int vmx_enable(void);
int vmx_disable(void);

/* Minimal VMCS instruction wrappers for the next VMM stage.  VMXON is
 * currently bound to the CPU that called vmx_enable(); callers must keep
 * VMCS operations on that CPU until vmx_disable() returns. */
int vmx_vmclear(uintptr_t vmcs_pa);
int vmx_vmptrld(uintptr_t vmcs_pa);
int vmx_vmread32(uint32_t field, uint32_t *value_store);
int vmx_vmwrite32(uint32_t field, uint32_t value);

#endif /* !__KERN_VIRT_VMX_H__ */
