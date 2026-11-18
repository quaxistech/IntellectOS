/* Native 64-bit kernel entry. Firmware boot services are already terminated.
 * SysV: RDI = physical BootInfo pointer, RSI = our CR3 page-table root.
 * Interrupts stay disabled until a real IDT/interrupt controller exists.
 */
.section .text.entry,"ax",@progbits
.globl intellect64_entry
.type intellect64_entry,@function
intellect64_entry:
    cli
    mov %rsi, %cr3
    xor %rbp, %rbp
    call intellect64_main
1:  cli
    hlt
    jmp 1b
.size intellect64_entry, .-intellect64_entry
.section .note.GNU-stack,"",@progbits
