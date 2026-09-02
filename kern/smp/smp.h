#ifndef __KERN_SMP_SMP_H__
#define __KERN_SMP_SMP_H__

#include <defs.h>
#include <smp_config.h>

void smp_init(void);
int smp_cpu_count(void);
int smp_cpu_online_count(void);
bool smp_is_enabled(void);
int smp_current_cpu(void);
void smp_lapic_eoi(void);

/* Entered by the real-mode AP trampoline after paging is enabled. */
void smp_ap_entry(uint32_t cpu_index) __attribute__((noreturn));

#endif /* !__KERN_SMP_SMP_H__ */
