#include <defs.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pmm.h>
#include <vmx.h>
#include "vmm.h"

static enum vmm_state current_vmm_state = VMM_STATE_UNINITIALIZED;

void
virt_init(void) {
    vmx_init();
    if (vmx_supported()) {
        current_vmm_state = VMM_STATE_READY;
        cprintf("vmm: VMX control plane ready (guest execution disabled)\n");
    }
    else {
        current_vmm_state = VMM_STATE_NO_VMX;
        cprintf("vmm: hardware virtualization is unavailable\n");
    }
}

enum vmm_state
vmm_state(void) {
    return current_vmm_state;
}

int
vmm_start(struct vmm_context *context) {
    int ret;
    if (context == NULL) {
        return -E_INVAL;
    }
    if (current_vmm_state == VMM_STATE_UNINITIALIZED) {
        virt_init();
    }
    if (current_vmm_state != VMM_STATE_READY) {
        return -E_NA_DEV;
    }
    ret = vmx_enable();
    if (ret != 0) {
        current_vmm_state = VMM_STATE_FAILED;
        context->state = VMM_STATE_FAILED;
        return ret;
    }
    context->state = VMM_STATE_RUNNING;
    context->vmexit_count = 0;
    current_vmm_state = VMM_STATE_RUNNING;
    return 0;
}

int
vmm_stop(struct vmm_context *context) {
    int ret;
    if (context == NULL || context->state != VMM_STATE_RUNNING) {
        return -E_INVAL;
    }
    ret = vmx_disable();
    if (ret == -E_BUSY) {
        /* VMXON is owned by another CPU.  The context is still live there;
         * do not turn a caller-placement error into a global VM failure. */
        return ret;
    }
    if (ret == 0) {
        context->state = VMM_STATE_READY;
        current_vmm_state = VMM_STATE_READY;
    }
    else {
        context->state = VMM_STATE_FAILED;
        current_vmm_state = VMM_STATE_FAILED;
    }
    return ret;
}

void
virt_selftest(void) {
    struct vmm_context context;
    struct Page *vmcs_page = NULL;
    uint32_t value;
    int ret;

    context.state = VMM_STATE_UNINITIALIZED;
    context.vmexit_count = 0;

    /* TCG and hosts without nested VMX must still have a deterministic,
     * non-destructive negative result. */
    if (!vmx_supported()) {
        ret = vmm_start(&context);
        if (ret != -E_NA_DEV) {
            panic("virt selftest: expected no-VMX result, got %d.\n", ret);
        }
        cprintf("virt selftest: VMX unavailable path passed.\n");
        return;
    }

    ret = vmm_start(&context);
    if (ret != 0) {
        panic("virt selftest: VMXON failed: %d.\n", ret);
    }
    if ((vmcs_page = alloc_page()) == NULL) {
        vmm_stop(&context);
        panic("virt selftest: VMCS allocation failed.\n");
    }
    memset(page2kva(vmcs_page), 0, PGSIZE);
    *(uint32_t *)page2kva(vmcs_page) = vmx_get_info()->revision;

    ret = vmx_vmclear(page2pa(vmcs_page));
    if (ret != 0) {
        vmm_stop(&context);
        free_page(vmcs_page);
        panic("virt selftest: VMCLEAR failed: %d.\n", ret);
    }
    ret = vmx_vmptrld(page2pa(vmcs_page));
    if (ret != 0) {
        vmm_stop(&context);
        free_page(vmcs_page);
        panic("virt selftest: VMPTRLD failed: %d.\n", ret);
    }
    ret = vmx_vmwrite32(VMX_VMCS_GUEST_RIP, 0x12345678);
    if (ret != 0 || vmx_vmread32(VMX_VMCS_GUEST_RIP, &value) != 0 ||
        value != 0x12345678) {
        vmm_stop(&context);
        free_page(vmcs_page);
        panic("virt selftest: VMCS read/write failed.\n");
    }
    ret = vmm_stop(&context);
    free_page(vmcs_page);
    if (ret != 0) {
        panic("virt selftest: VMXOFF failed: %d.\n", ret);
    }
    cprintf("virt selftest: VMXON/VMCS/VMXOFF path passed.\n");
}
