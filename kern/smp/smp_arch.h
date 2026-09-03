#ifndef __KERN_SMP_ARCH_H__
#define __KERN_SMP_ARCH_H__

/* Intel xAPIC architectural register and MP-specification constants. */
#define SMP_SIPI_MAX_PA              0x00100000
#define SMP_BDA_BASE_MEMORY_PA       0x00000413

#define SMP_MSR_IA32_APIC_BASE       0x0000001B
#define SMP_APIC_BASE_ENABLE         (1U << 11)
#define SMP_APIC_BASE_X2APIC         (1U << 10)

#define SMP_LAPIC_ID                 0x020
#define SMP_LAPIC_TPR                0x080
#define SMP_LAPIC_EOI                0x0B0
#define SMP_LAPIC_SVR                0x0F0
#define SMP_LAPIC_ESR                0x280
#define SMP_LAPIC_ICR_LOW            0x300
#define SMP_LAPIC_ICR_HIGH           0x310
#define SMP_LAPIC_LVT_TIMER          0x320
#define SMP_LAPIC_LVT_LINT0          0x350
#define SMP_LAPIC_LVT_LINT1          0x360

#define SMP_APIC_SVR_ENABLE          (1U << 8)
#define SMP_APIC_LVT_MASKED          (1U << 16)
#define SMP_APIC_LVT_EXTINT          (7U << 8)
#define SMP_APIC_LVT_NMI             (4U << 8)
#define SMP_APIC_ICR_DELIVERY_STATUS (1U << 12)
#define SMP_APIC_ICR_LEVEL_ASSERT    (1U << 14)
#define SMP_APIC_ICR_TRIGGER_LEVEL   (1U << 15)
#define SMP_APIC_DM_INIT             (5U << 8)
#define SMP_APIC_DM_STARTUP          (6U << 8)
#define SMP_APIC_SPURIOUS_VECTOR     0xFF

#define SMP_MP_FLOATING_SIGNATURE    "_MP_"
#define SMP_MP_CONFIG_SIGNATURE      "PCMP"
#define SMP_MP_ENTRY_PROCESSOR       0
#define SMP_MP_ENTRY_BUS             1
#define SMP_MP_ENTRY_IOAPIC          2
#define SMP_MP_ENTRY_INTSRC          3
#define SMP_MP_ENTRY_LOCAL_INTSRC    4
#define SMP_MP_CPU_ENABLED           0x01
#define SMP_MP_CPU_BOOTSTRAP         0x02

/* Keep the implementation names short while the values remain centralized. */
#define MSR_IA32_APIC_BASE           SMP_MSR_IA32_APIC_BASE
#define APIC_BASE_ENABLE             SMP_APIC_BASE_ENABLE
#define APIC_BASE_X2APIC             SMP_APIC_BASE_X2APIC
#define LAPIC_ID                     SMP_LAPIC_ID
#define LAPIC_TPR                    SMP_LAPIC_TPR
#define LAPIC_EOI                    SMP_LAPIC_EOI
#define LAPIC_SVR                    SMP_LAPIC_SVR
#define LAPIC_ESR                    SMP_LAPIC_ESR
#define LAPIC_ICR_LOW                SMP_LAPIC_ICR_LOW
#define LAPIC_ICR_HIGH               SMP_LAPIC_ICR_HIGH
#define LAPIC_LVT_TIMER              SMP_LAPIC_LVT_TIMER
#define LAPIC_LVT_LINT0              SMP_LAPIC_LVT_LINT0
#define LAPIC_LVT_LINT1              SMP_LAPIC_LVT_LINT1
#define APIC_SVR_ENABLE              SMP_APIC_SVR_ENABLE
#define APIC_LVT_MASKED              SMP_APIC_LVT_MASKED
#define APIC_LVT_EXTINT              SMP_APIC_LVT_EXTINT
#define APIC_LVT_NMI                 SMP_APIC_LVT_NMI
#define APIC_ICR_DELIVERY_STATUS     SMP_APIC_ICR_DELIVERY_STATUS
#define APIC_ICR_LEVEL_ASSERT        SMP_APIC_ICR_LEVEL_ASSERT
#define APIC_ICR_TRIGGER_LEVEL       SMP_APIC_ICR_TRIGGER_LEVEL
#define APIC_DM_INIT                 SMP_APIC_DM_INIT
#define APIC_DM_STARTUP              SMP_APIC_DM_STARTUP
#define APIC_SPURIOUS_VECTOR         SMP_APIC_SPURIOUS_VECTOR
#define MP_FLOATING_SIGNATURE        SMP_MP_FLOATING_SIGNATURE
#define MP_CONFIG_SIGNATURE          SMP_MP_CONFIG_SIGNATURE
#define MP_ENTRY_PROCESSOR           SMP_MP_ENTRY_PROCESSOR
#define MP_ENTRY_BUS                 SMP_MP_ENTRY_BUS
#define MP_ENTRY_IOAPIC              SMP_MP_ENTRY_IOAPIC
#define MP_ENTRY_INTSRC              SMP_MP_ENTRY_INTSRC
#define MP_ENTRY_LOCAL_INTSRC        SMP_MP_ENTRY_LOCAL_INTSRC
#define MP_CPU_ENABLED               SMP_MP_CPU_ENABLED
#define MP_CPU_BOOTSTRAP             SMP_MP_CPU_BOOTSTRAP

#endif /* !__KERN_SMP_ARCH_H__ */
