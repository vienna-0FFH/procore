#include <defs.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <x86.h>
#include <mmu.h>
#include <memlayout.h>
#include <pmm.h>
#include <smp.h>
#include <smp_config.h>
#include <smp_arch.h>
#include <trap.h>
#include <sched.h>

struct mp_floating_pointer {
    char signature[4];
    uint32_t config_pa;
    uint8_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature[5];
} __attribute__((packed));

struct mp_config_header {
    char signature[4];
    uint16_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table_pa;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t lapic_pa;
    uint16_t ext_length;
    uint8_t ext_checksum;
    uint8_t reserved;
} __attribute__((packed));

struct mp_processor_entry {
    uint8_t type;
    uint8_t apic_id;
    uint8_t apic_version;
    uint8_t flags;
    uint32_t signature;
    uint32_t feature;
    uint32_t reserved[2];
} __attribute__((packed));

struct mp_floating_pointer *smp_mpfp;
static uint8_t smp_apic_ids[SMP_MAX_CPUS];
static uint8_t smp_bsp_apic_id;
static int smp_ncpu = 1;
static volatile uint32_t smp_online[SMP_MAX_CPUS];
static struct Page *smp_ap_stacks[SMP_MAX_CPUS];
static uintptr_t smp_ap_stack_tops[SMP_MAX_CPUS];
static struct proc_struct *smp_current_procs[SMP_MAX_CPUS];
static struct proc_struct *smp_idle_procs[SMP_MAX_CPUS];
static uintptr_t smp_lapic_pa = SMP_LAPIC_DEFAULT_PA;
static uintptr_t smp_trampoline_pa = SMP_TRAMPOLINE_PA;
static volatile uint32_t smp_scheduler_started;
static bool smp_enabled;
static spinlock_t smp_ipi_lock;
static struct proc_struct *smp_switch_pending[SMP_MAX_CPUS];

/* Each CPU needs a private TSS because ring transitions use its kernel stack. */
static struct taskstate smp_tss[SMP_MAX_CPUS];
static struct segdesc smp_gdt[SMP_MAX_CPUS][SEG_TSS + 1];
static struct pseudodesc smp_gdt_pd[SMP_MAX_CPUS];
static volatile bool smp_arch_ready[SMP_MAX_CPUS];

extern char bootstacktop[];

extern char smp_trampoline_start[];
extern char smp_trampoline_end[];
extern char smp_trampoline_pm[];
extern char smp_trampoline_pm_farptr[];
extern char smp_trampoline_gdt[];
extern char smp_trampoline_gdt_desc[];
extern char smp_trampoline_cr3[];
extern char smp_trampoline_stack[];
extern char smp_trampoline_entry[];
extern char smp_trampoline_cpu[];

static volatile uint32_t *
smp_lapic_reg(uint32_t offset) {
    return (volatile uint32_t *)(SMP_LAPIC_VA + offset);
}

static uint32_t
smp_lapic_read(uint32_t offset) {
    return *smp_lapic_reg(offset);
}

static void
smp_lapic_write(uint32_t offset, uint32_t value) {
    *smp_lapic_reg(offset) = value;
}

struct proc_struct **
smp_current_ptr(void) {
    int cpu = smp_current_cpu();
    if (cpu < 0 || cpu >= SMP_MAX_CPUS) {
        cpu = 0;
    }
    return &smp_current_procs[cpu];
}

struct proc_struct **
smp_idle_ptr(void) {
    int cpu = smp_current_cpu();
    if (cpu < 0 || cpu >= SMP_MAX_CPUS) {
        cpu = 0;
    }
    return &smp_idle_procs[cpu];
}

void
smp_set_current(int cpu, struct proc_struct *proc) {
    if (cpu >= 0 && cpu < SMP_MAX_CPUS) {
        smp_current_procs[cpu] = proc;
    }
}

void
smp_set_idle(int cpu, struct proc_struct *proc) {
    if (cpu >= 0 && cpu < SMP_MAX_CPUS) {
        smp_idle_procs[cpu] = proc;
    }
}

void
smp_switch_begin(struct proc_struct *proc) {
    int cpu = smp_current_cpu();
    if (cpu >= 0 && cpu < SMP_MAX_CPUS) {
        smp_switch_pending[cpu] = proc;
    }
}

struct proc_struct *
smp_switch_take(void) {
    int cpu = smp_current_cpu();
    struct proc_struct *proc = NULL;
    if (cpu >= 0 && cpu < SMP_MAX_CPUS) {
        proc = smp_switch_pending[cpu];
        smp_switch_pending[cpu] = NULL;
    }
    return proc;
}

static void
smp_load_cpu_gdt(int cpu, uintptr_t esp0) {
    struct segdesc *gdt;
    struct pseudodesc *pd;

    gdt = smp_gdt[cpu];
    pd = &smp_gdt_pd[cpu];
    memset(&smp_tss[cpu], 0, sizeof(smp_tss[cpu]));
    smp_tss[cpu].ts_esp0 = esp0;
    smp_tss[cpu].ts_ss0 = KERNEL_DS;
    smp_tss[cpu].ts_iomb = sizeof(smp_tss[cpu]);
    gdt[0] = SEG_NULL;
    gdt[SEG_KTEXT] = SEG(STA_X | STA_R, 0, 0xFFFFFFFF, DPL_KERNEL);
    gdt[SEG_KDATA] = SEG(STA_W, 0, 0xFFFFFFFF, DPL_KERNEL);
    gdt[SEG_UTEXT] = SEG(STA_X | STA_R, 0, 0xFFFFFFFF, DPL_USER);
    gdt[SEG_UDATA] = SEG(STA_W, 0, 0xFFFFFFFF, DPL_USER);
    gdt[SEG_TSS] = SEGTSS(STS_T32A, (uintptr_t)&smp_tss[cpu],
                          sizeof(smp_tss[cpu]), DPL_KERNEL);
    pd->pd_lim = sizeof(smp_gdt[cpu]) - 1;
    pd->pd_base = (uintptr_t)gdt;

    asm volatile ("lgdt (%0)" :: "r"(pd) : "memory");
    asm volatile ("movw %%ax, %%gs" :: "a"(USER_DS));
    asm volatile ("movw %%ax, %%fs" :: "a"(USER_DS));
    asm volatile ("movw %%ax, %%es" :: "a"(KERNEL_DS));
    asm volatile ("movw %%ax, %%ds" :: "a"(KERNEL_DS));
    asm volatile ("movw %%ax, %%ss" :: "a"(KERNEL_DS));
    asm volatile ("ljmp %0, $1f\n 1:" :: "i"(KERNEL_CS));
    ltr(GD_TSS);
    smp_arch_ready[cpu] = 1;
}

void
smp_set_esp0(uintptr_t esp0) {
    int cpu = smp_current_cpu();
    if (cpu >= 0 && cpu < SMP_MAX_CPUS && smp_arch_ready[cpu]) {
        smp_tss[cpu].ts_esp0 = esp0;
    }
}

void
smp_lapic_eoi(void) {
    if (smp_enabled) {
        smp_lapic_write(LAPIC_EOI, 0);
    }
}

static inline uint64_t
smp_rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    return ((uint64_t)high << 32) | low;
}

static inline void
smp_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    asm volatile ("wrmsr" :: "c" (msr), "a" (low), "d" (high));
}

static inline void
smp_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t result[4]) {
    asm volatile (
        "cpuid"
        : "=a" (result[0]), "=b" (result[1]),
          "=c" (result[2]), "=d" (result[3])
        : "a" (leaf), "c" (subleaf));
}

static uint8_t
smp_checksum(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    while (length-- != 0) {
        sum = (uint8_t)(sum + *bytes++);
    }
    return sum;
}

static struct mp_floating_pointer *
smp_scan_range(uintptr_t start, size_t length) {
    uintptr_t end = start + length;
    for (; start + sizeof(struct mp_floating_pointer) <= end; start += 16) {
        struct mp_floating_pointer *mpfp = KADDR(start);
        if (memcmp(mpfp->signature, MP_FLOATING_SIGNATURE, 4) == 0) {
            size_t table_size = (size_t)mpfp->length * 16;
            uint8_t checksum = (mpfp->length == 0 || mpfp->length > 4 ||
                                 start + table_size > end) ? 0xff :
                smp_checksum(mpfp, table_size);
            if (checksum == 0) {
                return mpfp;
            }
        }
    }
    return NULL;
}

static struct mp_floating_pointer *
smp_find_mpfp(void) {
    uint16_t ebda_segment = *(volatile uint16_t *)KADDR(0x40E);
    uint16_t base_memory_kb = *(volatile uint16_t *)KADDR(0x413);
    struct mp_floating_pointer *mpfp;

    if (ebda_segment != 0 &&
        (mpfp = smp_scan_range((uintptr_t)ebda_segment << 4, 1024)) != NULL) {
        return mpfp;
    }
    if (base_memory_kb >= 1 &&
        (mpfp = smp_scan_range((uintptr_t)base_memory_kb * 1024 - 1024, 1024)) != NULL) {
        return mpfp;
    }
    return smp_scan_range(0xF0000, 0x10000);
}

static int
smp_select_trampoline(void) {
    uintptr_t image_size = (uintptr_t)(smp_trampoline_end - smp_trampoline_start);
    uint16_t base_memory_kb = *(volatile uint16_t *)KADDR(SMP_BDA_BASE_MEMORY_PA);
    uintptr_t candidate;
    uintptr_t conventional_top = (uintptr_t)base_memory_kb * 1024;

    /* The BIOS data area gives the top of conventional memory, below EBDA. */
    if (base_memory_kb >= 2) {
        candidate = ROUNDDOWN(conventional_top, PGSIZE);
        candidate -= PGSIZE;
        if (candidate >= PGSIZE && candidate + image_size <= SMP_SIPI_MAX_PA &&
            candidate + image_size <= conventional_top &&
            candidate + PGSIZE <= ((uintptr_t)npage << PGSHIFT)) {
            smp_trampoline_pa = candidate;
            return 0;
        }
    }

    /* Keep a compile-time fallback for firmware without a valid BDA value. */
    if (SMP_TRAMPOLINE_PA >= PGSIZE &&
        SMP_TRAMPOLINE_PA + image_size <= SMP_SIPI_MAX_PA &&
        SMP_TRAMPOLINE_PA + PGSIZE <= ((uintptr_t)npage << PGSHIFT)) {
        smp_trampoline_pa = SMP_TRAMPOLINE_PA;
        return 0;
    }
    return -E_NO_MEM;
}

static int
smp_parse_config(struct mp_floating_pointer *mpfp) {
    struct mp_config_header *header;
    uint8_t *entry;
    uint16_t i;
    size_t offset;
    size_t entry_size;
    uint8_t type;

    if (mpfp->config_pa == 0 || mpfp->feature[0] != 0) {
        return -E_NA_DEV;
    }
    if (mpfp->config_pa >= ((uintptr_t)npage << PGSHIFT)) {
        return -E_INVAL;
    }
    header = KADDR(mpfp->config_pa);
    if (memcmp(header->signature, MP_CONFIG_SIGNATURE, 4) != 0 ||
        header->length < sizeof(struct mp_config_header) ||
        header->length > ((uintptr_t)npage << PGSHIFT) - mpfp->config_pa ||
        smp_checksum(header, header->length) != 0) {
        return -E_INVAL;
    }

    smp_lapic_pa = header->lapic_pa != 0 ? header->lapic_pa : SMP_LAPIC_DEFAULT_PA;
    smp_ncpu = 0;
    smp_bsp_apic_id = 0;
    entry = (uint8_t *)(header + 1);
    offset = sizeof(struct mp_config_header);
    for (i = 0; i < header->entry_count; i++) {
        if (offset >= header->length) {
            return -E_INVAL;
        }
        type = entry[0];
        if (type == MP_ENTRY_PROCESSOR) {
            struct mp_processor_entry *processor = (struct mp_processor_entry *)entry;
            entry_size = 20;
            if (offset + entry_size > header->length) {
                return -E_INVAL;
            }
            if (processor->flags & MP_CPU_ENABLED) {
                if (smp_ncpu >= SMP_MAX_CPUS) {
                    return -E_TOO_BIG;
                }
                smp_apic_ids[smp_ncpu] = processor->apic_id;
                if (processor->flags & MP_CPU_BOOTSTRAP) {
                    smp_bsp_apic_id = processor->apic_id;
                }
                smp_ncpu++;
            }
        }
        else if (type == MP_ENTRY_BUS) {
            entry_size = 8;
        }
        else if (type == MP_ENTRY_IOAPIC || type == MP_ENTRY_INTSRC ||
                 type == MP_ENTRY_LOCAL_INTSRC) {
            entry_size = 8;
        }
        else {
            return -E_INVAL;
        }
        if (offset + entry_size > header->length) {
            return -E_INVAL;
        }
        entry += entry_size;
        offset += entry_size;
    }
    if (smp_ncpu == 0) {
        return -E_NA_DEV;
    }

    /* Keep the BSP at logical CPU zero. */
    for (i = 0; i < smp_ncpu; i++) {
        if (smp_apic_ids[i] == smp_bsp_apic_id) {
            uint8_t id = smp_apic_ids[0];
            smp_apic_ids[0] = smp_apic_ids[i];
            smp_apic_ids[i] = id;
            break;
        }
    }
    return 0;
}

/*
 * QEMU's modern machine types may expose CPU topology through CPUID without
 * installing a legacy MP table. The fallback is intentionally conservative:
 * it only accepts the flat, sequential APIC-ID topology used by the i386
 * machine and leaves more complex topologies to the MP-table path.
 */
static int
smp_discover_cpuid(void) {
    uint32_t result[4];
    uint32_t logical_count;
    uint32_t bsp_apic_id;
    uint32_t i;

    smp_cpuid(1, 0, result);
    logical_count = (result[1] >> 16) & 0xff;
    bsp_apic_id = (result[1] >> 24) & 0xff;
    if ((result[3] & (1U << 9)) == 0) {
        return -E_NA_DEV;
    }
    if (logical_count <= 1 || logical_count > SMP_MAX_CPUS) {
        return -E_NA_DEV;
    }
    smp_ncpu = logical_count;
    smp_bsp_apic_id = (uint8_t)bsp_apic_id;
    for (i = 0; i < (uint32_t)smp_ncpu; i++) {
        smp_apic_ids[i] = (uint8_t)(bsp_apic_id + i);
    }
    return 0;
}

static bool
smp_cpu_has_apic(void) {
    uint32_t result[4];
    smp_cpuid(1, 0, result);
    return (result[3] & (1U << 9)) != 0;
}

static void
smp_init_local_apic(bool bsp) {
    smp_lapic_write(LAPIC_TPR, 0);
    smp_lapic_write(LAPIC_SVR, APIC_SVR_ENABLE | APIC_SPURIOUS_VECTOR);
    smp_lapic_write(LAPIC_LVT_TIMER, APIC_LVT_MASKED);
    smp_lapic_write(LAPIC_LVT_LINT0, bsp ? APIC_LVT_EXTINT : APIC_LVT_MASKED);
    smp_lapic_write(LAPIC_LVT_LINT1, APIC_LVT_MASKED);
    smp_lapic_write(LAPIC_ESR, 0);
}

static void
smp_enable_local_apic(void) {
    uint64_t apic_base = smp_rdmsr(MSR_IA32_APIC_BASE);
    smp_wrmsr(MSR_IA32_APIC_BASE,
              (apic_base & ~((uint64_t)PGSIZE - 1) &
               ~(uint64_t)APIC_BASE_X2APIC) |
              (smp_lapic_pa & ~((uintptr_t)PGSIZE - 1)) |
              APIC_BASE_ENABLE);
}

static int
smp_map_lapic(void) {
    pte_t *ptep = get_pte(boot_pgdir, SMP_LAPIC_VA, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }
    *ptep = (smp_lapic_pa & ~(PGSIZE - 1)) | PTE_P | PTE_W |
            PTE_PWT | PTE_PCD;
    invlpg((void *)SMP_LAPIC_VA);
    return 0;
}

static int
smp_lapic_wait_icr(void) {
    uint32_t i;
    for (i = 0; i < SMP_LAPIC_WAIT_LOOPS; i++) {
        if ((smp_lapic_read(LAPIC_ICR_LOW) & APIC_ICR_DELIVERY_STATUS) == 0) {
            return 0;
        }
        asm volatile ("pause");
    }
    return -E_TIMEOUT;
}

static int
smp_lapic_send_ipi(uint8_t apic_id, uint32_t command) {
    int ret;

    spin_lock(&smp_ipi_lock);
    smp_lapic_write(LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
    smp_lapic_write(LAPIC_ICR_LOW, command);
    ret = smp_lapic_wait_icr();
    spin_unlock(&smp_ipi_lock);
    return ret;
}

void
smp_send_reschedule(void) {
    int cpu;
    if (!smp_enabled) {
        return;
    }
    for (cpu = 1; cpu < smp_ncpu; cpu++) {
        if (smp_online[cpu] != 0) {
            smp_lapic_send_ipi(smp_apic_ids[cpu], SMP_IPI_RESCHEDULE_VECTOR);
        }
    }
}

void
smp_send_reschedule_cpu(int cpu_index) {
    int current_cpu;

    if (!smp_enabled || cpu_index < 0 || cpu_index >= smp_ncpu ||
        smp_online[cpu_index] == 0) {
        return;
    }
    current_cpu = smp_current_cpu();
    if (cpu_index == current_cpu) {
        return;
    }
    smp_lapic_send_ipi(smp_apic_ids[cpu_index], SMP_IPI_RESCHEDULE_VECTOR);
}

static void
smp_delay(void) {
    uint32_t i;
    for (i = 0; i < SMP_AP_DELAY_LOOPS; i++) {
        asm volatile ("pause");
    }
}

static int
smp_prepare_trampoline(uint32_t cpu_index, uintptr_t stack_top) {
    uintptr_t image_size = (uintptr_t)(smp_trampoline_end - smp_trampoline_start);
    uintptr_t pm_farptr_offset = (uintptr_t)(smp_trampoline_pm_farptr - smp_trampoline_start);
    uintptr_t gdt_desc_offset = (uintptr_t)(smp_trampoline_gdt_desc - smp_trampoline_start);
    uintptr_t gdt_offset;
    char *image;

    if (image_size > PGSIZE) {
        return -E_TOO_BIG;
    }
    image = KADDR(smp_trampoline_pa);
    memcpy(image, smp_trampoline_start, image_size);

    *(uint32_t *)(image + pm_farptr_offset) = smp_trampoline_pa +
        (uintptr_t)(smp_trampoline_pm - smp_trampoline_start);

    *(uint32_t *)(image + (smp_trampoline_cr3 - smp_trampoline_start)) = boot_cr3;
    *(uint32_t *)(image + (smp_trampoline_stack - smp_trampoline_start)) = stack_top;
    *(uint32_t *)(image + (smp_trampoline_entry - smp_trampoline_start)) =
        (uint32_t)&smp_ap_entry;
    *(uint32_t *)(image + (smp_trampoline_cpu - smp_trampoline_start)) = cpu_index;

    gdt_offset = (uintptr_t)(smp_trampoline_gdt_desc - smp_trampoline_start);
    *(uint32_t *)(image + gdt_desc_offset + 2) = smp_trampoline_pa + gdt_offset +
        (uintptr_t)(smp_trampoline_gdt - smp_trampoline_gdt_desc);
    return 0;
}

static int
smp_boot_ap(uint32_t cpu_index) {
    int ret;
    uint32_t timeout;
    ret = smp_lapic_send_ipi(smp_apic_ids[cpu_index],
                             APIC_DM_INIT | APIC_ICR_LEVEL_ASSERT |
                             APIC_ICR_TRIGGER_LEVEL);
    if (ret != 0) {
        return ret;
    }
    smp_delay();
    ret = smp_lapic_send_ipi(smp_apic_ids[cpu_index],
                             APIC_DM_INIT | APIC_ICR_TRIGGER_LEVEL);
    if (ret != 0) {
        return ret;
    }
    smp_delay();
    ret = smp_lapic_send_ipi(smp_apic_ids[cpu_index],
                             APIC_DM_STARTUP | (smp_trampoline_pa >> PGSHIFT));
    if (ret != 0) {
        return ret;
    }
    smp_delay();
    ret = smp_lapic_send_ipi(smp_apic_ids[cpu_index],
                             APIC_DM_STARTUP | (smp_trampoline_pa >> PGSHIFT));
    if (ret != 0) {
        return ret;
    }

    for (timeout = 0; smp_online[cpu_index] == 0 &&
         timeout < SMP_AP_STARTUP_TIMEOUT;
         timeout++) {
        asm volatile ("pause");
    }
    if (smp_online[cpu_index] == 0) {
        return -E_TIMEOUT;
    }
    return 0;
}

void
smp_ap_entry(uint32_t cpu_index) {
    /* The trampoline's temporary GDT has the same flat segments as the
     * kernel GDT.  Install the shared IDT before declaring the AP online so
     * an unexpected interrupt cannot use the reset-time null IDT. */
    if (cpu_index >= SMP_MAX_CPUS) {
        for (;;) {
            asm volatile ("hlt");
        }
    }
    smp_load_cpu_gdt(cpu_index, smp_ap_stack_tops[cpu_index]);
    idt_init();
    smp_enable_local_apic();
    smp_init_local_apic(0);
    if (cpu_index < SMP_MAX_CPUS) {
        barrier();
        smp_online[cpu_index] = 1;
    }
    barrier();
    while (smp_scheduler_started == 0) {
        asm volatile ("pause");
    }
    smp_set_current(cpu_index, smp_idle_procs[cpu_index]);
    sched_cpu_idle();
}

void
smp_init(void) {
    struct mp_floating_pointer *mpfp;
    uint32_t i;
    int ret, cpuid_ret;

    memset((void *)smp_online, 0, sizeof(smp_online));
    smp_online[0] = 1;
    smp_ncpu = 1;
    smp_enabled = 0;
    smp_scheduler_started = 0;
    spin_init(&smp_ipi_lock);
    smp_load_cpu_gdt(0, (uintptr_t)bootstacktop);

    mpfp = smp_find_mpfp();
    ret = (mpfp != NULL) ? smp_parse_config(mpfp) : -E_NA_DEV;
    /* Some firmware tables describe only the BSP.  CPUID still gives the
     * actual logical CPU count exposed by the machine, so use it when the
     * table did not find an AP. */
    if (!smp_cpu_has_apic()) {
        smp_ncpu = 1;
        ret = -E_NA_DEV;
    }
    else if (ret != 0 || smp_ncpu <= 1) {
        if (ret != 0) {
            smp_ncpu = 1;
            smp_lapic_pa = SMP_LAPIC_DEFAULT_PA;
        }
        cpuid_ret = smp_discover_cpuid();
        if (cpuid_ret == 0) {
            ret = 0;
        }
    }
    if (ret != 0 || smp_ncpu <= 1) {
        smp_ncpu = 1;
        cprintf("smp: no additional processors, using 1 CPU\n");
        return;
    }
    smp_mpfp = mpfp;

    ret = smp_map_lapic();
    if (ret != 0) {
        cprintf("smp: LAPIC mapping failed (%d), using 1 CPU\n", ret);
        smp_ncpu = 1;
        return;
    }

    ret = smp_select_trampoline();
    if (ret != 0) {
        cprintf("smp: no usable low-memory trampoline (%d), using 1 CPU\n", ret);
        smp_ncpu = 1;
        return;
    }

    smp_enable_local_apic();
    smp_init_local_apic(1);

    /* AP execution enables paging while the low identity mapping is present. */
    boot_pgdir[0] = boot_pgdir[PDX(KERNBASE)];
    for (i = 1; i < (uint32_t)smp_ncpu; i++) {
        if ((smp_ap_stacks[i] = alloc_pages(KSTACKPAGE)) == NULL) {
            cprintf("smp: AP%d stack allocation failed\n", i);
            smp_ncpu = i;
            break;
        }
        smp_ap_stack_tops[i] = (uintptr_t)page2kva(smp_ap_stacks[i]) + KSTACKSIZE;
        ret = smp_prepare_trampoline(i,
                smp_ap_stack_tops[i]);
        if (ret == 0) {
            ret = smp_boot_ap(i);
        }
        if (ret != 0) {
            cprintf("smp: AP%d (APIC %d) failed to start (%d)\n",
                    i, smp_apic_ids[i], ret);
            smp_ncpu = i;
            break;
        }
        cprintf("smp: CPU%d online (APIC %d)\n", i, smp_apic_ids[i]);
    }
    boot_pgdir[0] = 0;
    invlpg((void *)0);
    smp_enabled = (smp_ncpu > 1);
    cprintf("smp: %d CPU(s) online\n", smp_cpu_online_count());
}

int
smp_cpu_count(void) {
    return smp_ncpu;
}

int
smp_cpu_online_count(void) {
    int i, count = 0;
    for (i = 0; i < smp_ncpu; i++) {
        if (smp_online[i] != 0) {
            count++;
        }
    }
    return count;
}

bool
smp_is_enabled(void) {
    return smp_enabled;
}

void
smp_start_cpus(void) {
    barrier();
    smp_scheduler_started = 1;
    barrier();
    smp_send_reschedule();
}

int
smp_current_cpu(void) {
    uint8_t apic_id;
    int i;
    if (!smp_enabled) {
        return 0;
    }
    apic_id = (uint8_t)(smp_lapic_read(LAPIC_ID) >> 24);
    for (i = 0; i < smp_ncpu; i++) {
        if (smp_apic_ids[i] == apic_id) {
            return i;
        }
    }
    return -1;
}
