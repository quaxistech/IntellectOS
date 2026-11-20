.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
    xor %rbp,%rbp
    call main
    mov %eax,%edi
    mov $5,%eax
    int $0x80
1:  jmp 1b
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
