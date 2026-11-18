/* Exception vectors with a consistent stack frame: vector, error, EIP, CS, EFLAGS.
 * Fault handlers halt; no attempt at recovery or IRQ enable.
 */
.code32
.section .text,"ax",@progbits
.extern kernel_panic_exception
.globl isr0
isr0:
    pushl $0
    pushl $0
    jmp isr_common
.globl isr1
isr1:
    pushl $0
    pushl $1
    jmp isr_common
.globl isr2
isr2:
    pushl $0
    pushl $2
    jmp isr_common
.globl isr3
isr3:
    pushl $0
    pushl $3
    jmp isr_common
.globl isr4
isr4:
    pushl $0
    pushl $4
    jmp isr_common
.globl isr5
isr5:
    pushl $0
    pushl $5
    jmp isr_common
.globl isr6
isr6:
    pushl $0
    pushl $6
    jmp isr_common
.globl isr7
isr7:
    pushl $0
    pushl $7
    jmp isr_common
.globl isr8
isr8:
    pushl $8
    jmp isr_common
.globl isr9
isr9:
    pushl $0
    pushl $9
    jmp isr_common
.globl isr10
isr10:
    pushl $10
    jmp isr_common
.globl isr11
isr11:
    pushl $11
    jmp isr_common
.globl isr12
isr12:
    pushl $12
    jmp isr_common
.globl isr13
isr13:
    pushl $13
    jmp isr_common
.globl isr14
isr14:
    pushl $14
    jmp isr_common
.globl isr15
isr15:
    pushl $0
    pushl $15
    jmp isr_common
.globl isr16
isr16:
    pushl $0
    pushl $16
    jmp isr_common
.globl isr17
isr17:
    pushl $17
    jmp isr_common
.globl isr18
isr18:
    pushl $0
    pushl $18
    jmp isr_common
.globl isr19
isr19:
    pushl $0
    pushl $19
    jmp isr_common
.globl isr20
isr20:
    pushl $0
    pushl $20
    jmp isr_common
.globl isr21
isr21:
    pushl $21
    jmp isr_common
.globl isr22
isr22:
    pushl $0
    pushl $22
    jmp isr_common
.globl isr23
isr23:
    pushl $0
    pushl $23
    jmp isr_common
.globl isr24
isr24:
    pushl $0
    pushl $24
    jmp isr_common
.globl isr25
isr25:
    pushl $0
    pushl $25
    jmp isr_common
.globl isr26
isr26:
    pushl $0
    pushl $26
    jmp isr_common
.globl isr27
isr27:
    pushl $0
    pushl $27
    jmp isr_common
.globl isr28
isr28:
    pushl $0
    pushl $28
    jmp isr_common
.globl isr29
isr29:
    pushl $29
    jmp isr_common
.globl isr30
isr30:
    pushl $30
    jmp isr_common
.globl isr31
isr31:
    pushl $0
    pushl $31
    jmp isr_common
.globl unexpected_isr
unexpected_isr:
    pushl $0
    pushl $255
    jmp isr_common

isr_common:
    cli
    pusha
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movl %esp,%eax
    addl $32,%eax
    pushl %eax
    call kernel_panic_exception
1:  cli
    hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
