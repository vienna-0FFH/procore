#ifndef __KERN_SMP_SMP_H__
#define __KERN_SMP_SMP_H__

#include <defs.h>
#include <mmu.h>
#include <smp_config.h>

struct proc_struct;
struct cpu_stat;

void smp_init(void);
int smp_cpu_count(void);
int smp_cpu_online_count(void);
bool smp_is_enabled(void);
int smp_current_cpu(void);
void smp_lapic_eoi(void);
struct proc_struct **smp_current_ptr(void);
struct proc_struct **smp_idle_ptr(void);
void smp_set_current(int cpu, struct proc_struct *proc);
void smp_publish_cr3(int cpu, uintptr_t cr3);
void smp_set_idle(int cpu, struct proc_struct *proc);
void smp_set_esp0(uintptr_t esp0);
void smp_start_cpus(void);
void smp_send_reschedule(void);
void smp_send_reschedule_cpu(int cpu_index);
void smp_tlb_shootdown(pde_t *pgdir, uintptr_t la);
void smp_handle_tlb_ipi(void);
uint32_t smp_online_cpu_mask(void);
void smp_record_tick(int cpu, bool idle);
void smp_record_switch(int cpu);
void smp_record_migration(int cpu);
int smp_get_cpu_stat(int cpu, struct cpu_stat *stat);
void smp_switch_begin(struct proc_struct *proc);
struct proc_struct *smp_switch_take(void);

/* Entered by the real-mode AP trampoline after paging is enabled. */
void smp_ap_entry(uint32_t cpu_index) __attribute__((noreturn));

#endif /* !__KERN_SMP_SMP_H__ */
