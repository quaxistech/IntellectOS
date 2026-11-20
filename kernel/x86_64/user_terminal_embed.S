/* Build-generated flat user text, never executed in ring0. */
.section .rodata,"a",@progbits
.balign 16
.globl intellect_user_terminal_begin
intellect_user_terminal_begin:
.incbin "build/user/terminal.bin"
.globl intellect_user_terminal_end
intellect_user_terminal_end:
.section .note.GNU-stack,"",@progbits
