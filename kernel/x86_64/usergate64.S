/* Single-process ring3 gate. RDI=user RIP, RSI=user RSP.
 * iretq drops CPL; int 0x80 enters via TSS.RSP0; no interrupts or syscall MSRs.
 * The saved kernel stack is resumed on user SYS_EXIT or user fault.
 */
.section .bss,"aw",@nobits
.balign 8
saved_kernel_stack_u1: .quad 0
.section .text,"ax",@progbits
.globl intellect_u1_enter
.type intellect_u1_enter,@function
intellect_u1_enter:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,saved_kernel_stack_u1(%rip)
    movw $0x2b,%ax
    movw %ax,%ds
    movw %ax,%es
    pushq $0x2b          /* user SS */
    pushq %rsi           /* user RSP */
    pushfq
    popq %rax
    andq $-513,%rax      /* IF=0 while no IRQ architecture exists */
    pushq %rax
    pushq $0x33          /* user CS */
    pushq %rdi           /* user RIP */
    iretq
.size intellect_u1_enter,.-intellect_u1_enter
.globl intellect_u1_resume_kernel
.type intellect_u1_resume_kernel,@function
intellect_u1_resume_kernel:
    cli
    movq saved_kernel_stack_u1(%rip),%rsp
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    retq
.size intellect_u1_resume_kernel,.-intellect_u1_resume_kernel
.globl intellect_u1_int80
.type intellect_u1_int80,@function
intellect_u1_int80:
    cld
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    movq %rsp,%rdi
    movq %rsp,%rbx     /* callee-saved trap frame pointer across C call */
    andq $-16,%rsp
    call intellect_u1_syscall_dispatch
    /* RAX=1 means the user requested exit; restores pre-iret kernel frame. */
    testl %eax,%eax
    jnz intellect_u1_resume_kernel
    /* Restore the syscall frame after call; RSP may have been aligned down. */
    movq %rbx,%rsp    /* restore exact original stack before popping GPRs */
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    iretq
.size intellect_u1_int80,.-intellect_u1_int80
.section .note.GNU-stack,"",@progbits
