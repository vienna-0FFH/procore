#include <mmu.h>
#include <memlayout.h>

/*
 * This image is copied to a page below 1 MiB and entered with INIT-SIPI-SIPI.
 * All references to data in the image are offsets from the physical image
 * base kept in EBP. The C bootstrap code patches the fields before starting
 * an AP.
 */
.text
.code16
.globl smp_trampoline_start
.globl smp_trampoline_end
.globl smp_trampoline_pm
.globl smp_trampoline_pm_farptr
.globl smp_trampoline_gdt_desc
.globl smp_trampoline_gdt
.globl smp_trampoline_cr3
.globl smp_trampoline_stack
.globl smp_trampoline_entry
.globl smp_trampoline_cpu

smp_trampoline_start:
    cli
    cld
    xorl %eax, %eax
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es

    /* Keep the physical base while CS still has its real-mode base. */
    movzwl %ax, %ebp
    shll $4, %ebp

    movw $(smp_trampoline_gdt_desc - smp_trampoline_start), %si
    lgdt (%si)

    movl %cr0, %eax
    orl $CR0_PE, %eax
    movl %eax, %cr0
    /* The protected-mode target is patched to the selected low-memory page
     * by the BSP before sending the startup IPI. */
    movw $(smp_trampoline_pm_farptr - smp_trampoline_start), %si
    ljmpl *(%si)

.code32
smp_trampoline_pm:
    movw $KERNEL_DS, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl (smp_trampoline_cr3 - smp_trampoline_start)(%ebp), %eax
    movl %eax, %cr3
    movl %cr0, %eax
    orl $CR0_PG, %eax
    movl %eax, %cr0

    /* The identity mapping stays present until all APs reach C code. */
    movl (smp_trampoline_stack - smp_trampoline_start)(%ebp), %esp
    movl (smp_trampoline_entry - smp_trampoline_start)(%ebp), %eax
    pushl (smp_trampoline_cpu - smp_trampoline_start)(%ebp)
    call *%eax

smp_trampoline_hang:
    pause
    jmp smp_trampoline_hang

.align 4
smp_trampoline_pm_farptr:
    .long 0
    .word KERNEL_CS

.align 4
smp_trampoline_gdt:
    SEG_NULL
    SEG_ASM(STA_X | STA_R, 0, 0xFFFFFFFF)
    SEG_ASM(STA_W, 0, 0xFFFFFFFF)

smp_trampoline_gdt_desc:
    .word (smp_trampoline_gdt - smp_trampoline_start) - 1
    .long smp_trampoline_gdt - smp_trampoline_start

.align 4
smp_trampoline_cr3:
    .long 0
smp_trampoline_stack:
    .long 0
smp_trampoline_entry:
    .long 0
smp_trampoline_cpu:
    .long 0

smp_trampoline_end:
