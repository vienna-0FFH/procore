.text
.globl switch_to
.extern proc_switch_out_context
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

    # Change to the incoming stack before marking the outgoing context
    # quiescent.  The helper receives the saved context pointer.
    movl %eax, %esi             # esi = from context
    movl 4(%esp), %edi          # edi = to context
    movl 4(%edi), %edx
    movl %edx, %esp
    pushl %esi
    call proc_switch_out_context
    addl $4, %esp

    # restore to's registers
    movl 28(%edi), %ebp
    movl 20(%edi), %esi
    movl 16(%edi), %edx
    movl 12(%edi), %ecx
    movl 24(%edi), %eax
    movl 8(%edi), %ebx
    movl 4(%edi), %esp

    pushl 0(%edi)
    movl %eax, %edi

    ret
