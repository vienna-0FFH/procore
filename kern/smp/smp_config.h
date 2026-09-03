#ifndef __KERN_SMP_CONFIG_H__
#define __KERN_SMP_CONFIG_H__

/*
 * Platform policy knobs.  Each default can be overridden by the build
 * command, for example: SMP_DEFS+=-DSMP_MAX_CPUS=4.
 *
 * SMP_TRAMPOLINE_PA and SMP_LAPIC_VA must refer to free, page-aligned
 * addresses in the low-memory identity map and kernel address space.
 */
#ifndef SMP_MAX_CPUS
#define SMP_MAX_CPUS                 8
#endif

#ifndef SMP_TRAMPOLINE_PA
#define SMP_TRAMPOLINE_PA            0x00007000
#endif

#ifndef SMP_LAPIC_DEFAULT_PA
#define SMP_LAPIC_DEFAULT_PA         0xFEE00000
#endif

#ifndef SMP_LAPIC_VA
#define SMP_LAPIC_VA                 0xFEE00000
#endif

#ifndef SMP_LAPIC_WAIT_LOOPS
#define SMP_LAPIC_WAIT_LOOPS          1000000U
#endif

#ifndef SMP_AP_DELAY_LOOPS
#define SMP_AP_DELAY_LOOPS            100000U
#endif

#ifndef SMP_AP_STARTUP_TIMEOUT
#define SMP_AP_STARTUP_TIMEOUT        50000000U
#endif

#ifndef SMP_IPI_RESCHEDULE_VECTOR
#define SMP_IPI_RESCHEDULE_VECTOR    0xF0
#endif


#if SMP_MAX_CPUS < 1
#error "SMP_MAX_CPUS must be positive"
#endif

#endif /* !__KERN_SMP_CONFIG_H__ */
