/* The ABI boundary: Microsoft x64 UEFI calling convention -> SysV kernel.
 * Called with RCX = kernel phys, RDX = stack top, R8 = BootInfo, R9 = CR3.
 * No UEFI calls or return are possible after this instruction sequence.
 */
.text
.globl intellect_efi_jump
intellect_efi_jump:
    cli
    mov %rcx, %rax
    mov %rdx, %rsp
    and $-16, %rsp
    mov %r8, %rdi
    mov %r9, %rsi
    jmp *%rax
