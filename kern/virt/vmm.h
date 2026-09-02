#ifndef __KERN_VIRT_VMM_H__
#define __KERN_VIRT_VMM_H__

#include <defs.h>

enum vmm_state {
    VMM_STATE_UNINITIALIZED = 0,
    VMM_STATE_NO_VMX,
    VMM_STATE_READY,
    VMM_STATE_RUNNING,
    VMM_STATE_FAILED,
};

/* A small control-plane object. Guest memory and device emulation come later. */
struct vmm_context {
    enum vmm_state state;
    uint32_t vmexit_count;
};

void virt_init(void);
enum vmm_state vmm_state(void);
int vmm_start(struct vmm_context *context);
int vmm_stop(struct vmm_context *context);

#endif /* !__KERN_VIRT_VMM_H__ */
