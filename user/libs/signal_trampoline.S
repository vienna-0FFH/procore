.text
.globl __ucore_signal_trampoline
__ucore_signal_trampoline:
    movl $156, %eax
    int $0x80
    hlt
    jmp __ucore_signal_trampoline
