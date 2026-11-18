/* Assemble as x86-64 COFF; embed our freestanding flat kernel in the EFI file. */
.section .rdata,"dr"
.p2align 4
.globl intellect_kernel64_start
intellect_kernel64_start:
.incbin "build/kernel64.bin"
.globl intellect_kernel64_end
intellect_kernel64_end:
