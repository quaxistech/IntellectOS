/* CPU arrives here in 32-bit protected mode, paging and interrupts off.
 * Code/data segments are flat; BIOS calls are no longer permitted.
 */
.code32
.section .text.start,"ax",@progbits
.globl _start
.extern kernel_main
_start:
    movl $0x70000, %esp
    xorl %ebp, %ebp

    /* Clear .bss including data structures and exception vectors. */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    cld
    rep stosb

    pushl $0x5000
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
.section .note.GNU-stack,"",@progbits
