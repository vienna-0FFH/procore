#ifndef __KERN_SMP_SMP_H__
#define __KERN_SMP_SMP_H__

#include <defs.h>

#define SMP_MAX_CPUS            8

void smp_init(void);
int smp_cpu_count(void);
int smp_cpu_online_count(void);
bool smp_is_enabled(void);

/* Entered by the real-mode AP trampoline after paging is enabled. */
void smp_ap_entry(uint32_t cpu_index) __attribute__((noreturn));

#endif /* !__KERN_SMP_SMP_H__ */
