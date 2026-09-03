.text
.globl switch_to
.extern proc_switch_complete
switch_to:                      # switch_to(from, to)

    # save from's registers
    movl 4(%esp), %eax          # eax points to from
    popl 0(%eax)                # save eip !popl
    movl %esp, 4(%eax)
    movl %ebx, 8(%eax)
    movl %ecx, 12(%eax)
    movl %edx, 16(%eax)
    movl %esi, 20(%eax)
    movl %edi, 24(%eax)
    movl %ebp, 28(%eax)

    # Switch to the incoming stack before retiring the outgoing task.  Keep
    # the destination context in %esi: the C helper follows the i386 ABI and
    # preserves callee-saved registers, while the remaining registers are
    # restored only after the helper returns.
    movl 4(%esp), %esi          # esi points to to
    movl 4(%esi), %esp          # enter to's saved stack
    pushl 0(%esi)               # preserve to's resume eip across the call
    call proc_switch_complete

    # Restore to's registers and resume at its saved instruction pointer.
    movl 0(%esp), %eax          # keep the saved eip while restoring %esi
    addl $4, %esp               # restore to's original saved stack pointer
    movl 28(%esi), %ebp
    movl 24(%esi), %edi
    movl 16(%esi), %edx
    movl 12(%esi), %ecx
    movl 8(%esi), %ebx
    movl 20(%esi), %esi
    pushl %eax                  # return through to's saved eip
    ret
