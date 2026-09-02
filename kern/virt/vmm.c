#include <defs.h>
#include <error.h>
#include <stdio.h>
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
