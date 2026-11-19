/* Position-independent x86-64 exception entry. No external firmware calls.
 * Every handler presents [vector,error,RIP,CS,RFLAGS] to the common stub.
 * The vector table consists of signed relative displacements (no absolute
 * relocations, compatible with the flat position-independent image).
 */
.section .text,"ax",@progbits
.type intellect64_exception_0,@function
intellect64_exception_0:
    pushq $0
    pushq $0
    jmp intellect64_exception_common
.type intellect64_exception_1,@function
intellect64_exception_1:
    pushq $0
    pushq $1
    jmp intellect64_exception_common
.type intellect64_exception_2,@function
intellect64_exception_2:
    pushq $0
    pushq $2
    jmp intellect64_exception_common
.type intellect64_exception_3,@function
intellect64_exception_3:
    pushq $0
    pushq $3
    jmp intellect64_exception_common
.type intellect64_exception_4,@function
intellect64_exception_4:
    pushq $0
    pushq $4
    jmp intellect64_exception_common
.type intellect64_exception_5,@function
intellect64_exception_5:
    pushq $0
    pushq $5
    jmp intellect64_exception_common
.type intellect64_exception_6,@function
intellect64_exception_6:
    pushq $0
    pushq $6
    jmp intellect64_exception_common
.type intellect64_exception_7,@function
intellect64_exception_7:
    pushq $0
    pushq $7
    jmp intellect64_exception_common
.type intellect64_exception_8,@function
intellect64_exception_8:
    pushq $8
    jmp intellect64_exception_common
.type intellect64_exception_9,@function
intellect64_exception_9:
    pushq $0
    pushq $9
    jmp intellect64_exception_common
.type intellect64_exception_10,@function
intellect64_exception_10:
    pushq $10
    jmp intellect64_exception_common
.type intellect64_exception_11,@function
intellect64_exception_11:
    pushq $11
    jmp intellect64_exception_common
.type intellect64_exception_12,@function
intellect64_exception_12:
    pushq $12
    jmp intellect64_exception_common
.type intellect64_exception_13,@function
intellect64_exception_13:
    pushq $13
    jmp intellect64_exception_common
.type intellect64_exception_14,@function
intellect64_exception_14:
    pushq $14
    jmp intellect64_exception_common
.type intellect64_exception_15,@function
intellect64_exception_15:
    pushq $0
    pushq $15
    jmp intellect64_exception_common
.type intellect64_exception_16,@function
intellect64_exception_16:
    pushq $0
    pushq $16
    jmp intellect64_exception_common
.type intellect64_exception_17,@function
intellect64_exception_17:
    pushq $17
    jmp intellect64_exception_common
.type intellect64_exception_18,@function
intellect64_exception_18:
    pushq $0
    pushq $18
    jmp intellect64_exception_common
.type intellect64_exception_19,@function
intellect64_exception_19:
    pushq $0
    pushq $19
    jmp intellect64_exception_common
.type intellect64_exception_20,@function
intellect64_exception_20:
    pushq $0
    pushq $20
    jmp intellect64_exception_common
.type intellect64_exception_21,@function
intellect64_exception_21:
    pushq $21
    jmp intellect64_exception_common
.type intellect64_exception_22,@function
intellect64_exception_22:
    pushq $0
    pushq $22
    jmp intellect64_exception_common
.type intellect64_exception_23,@function
intellect64_exception_23:
    pushq $0
    pushq $23
    jmp intellect64_exception_common
.type intellect64_exception_24,@function
intellect64_exception_24:
    pushq $0
    pushq $24
    jmp intellect64_exception_common
.type intellect64_exception_25,@function
intellect64_exception_25:
    pushq $0
    pushq $25
    jmp intellect64_exception_common
.type intellect64_exception_26,@function
intellect64_exception_26:
    pushq $0
    pushq $26
    jmp intellect64_exception_common
.type intellect64_exception_27,@function
intellect64_exception_27:
    pushq $0
    pushq $27
    jmp intellect64_exception_common
.type intellect64_exception_28,@function
intellect64_exception_28:
    pushq $0
    pushq $28
    jmp intellect64_exception_common
.type intellect64_exception_29,@function
intellect64_exception_29:
    pushq $29
    jmp intellect64_exception_common
.type intellect64_exception_30,@function
intellect64_exception_30:
    pushq $30
    jmp intellect64_exception_common
.type intellect64_exception_31,@function
intellect64_exception_31:
    pushq $0
    pushq $31
    jmp intellect64_exception_common
intellect64_exception_common:
    cli
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,%rdi
    andq $-16,%rsp
    call intellect64_exception_dispatch
.Lstop:
    cli
    hlt
    jmp .Lstop
.section .rodata,"a",@progbits
.balign 4
.globl intellect64_exception_offsets
.type intellect64_exception_offsets,@object
intellect64_exception_offsets:
    .long intellect64_exception_0-intellect64_exception_offsets
    .long intellect64_exception_1-intellect64_exception_offsets
    .long intellect64_exception_2-intellect64_exception_offsets
    .long intellect64_exception_3-intellect64_exception_offsets
    .long intellect64_exception_4-intellect64_exception_offsets
    .long intellect64_exception_5-intellect64_exception_offsets
    .long intellect64_exception_6-intellect64_exception_offsets
    .long intellect64_exception_7-intellect64_exception_offsets
    .long intellect64_exception_8-intellect64_exception_offsets
    .long intellect64_exception_9-intellect64_exception_offsets
    .long intellect64_exception_10-intellect64_exception_offsets
    .long intellect64_exception_11-intellect64_exception_offsets
    .long intellect64_exception_12-intellect64_exception_offsets
    .long intellect64_exception_13-intellect64_exception_offsets
    .long intellect64_exception_14-intellect64_exception_offsets
    .long intellect64_exception_15-intellect64_exception_offsets
    .long intellect64_exception_16-intellect64_exception_offsets
    .long intellect64_exception_17-intellect64_exception_offsets
    .long intellect64_exception_18-intellect64_exception_offsets
    .long intellect64_exception_19-intellect64_exception_offsets
    .long intellect64_exception_20-intellect64_exception_offsets
    .long intellect64_exception_21-intellect64_exception_offsets
    .long intellect64_exception_22-intellect64_exception_offsets
    .long intellect64_exception_23-intellect64_exception_offsets
    .long intellect64_exception_24-intellect64_exception_offsets
    .long intellect64_exception_25-intellect64_exception_offsets
    .long intellect64_exception_26-intellect64_exception_offsets
    .long intellect64_exception_27-intellect64_exception_offsets
    .long intellect64_exception_28-intellect64_exception_offsets
    .long intellect64_exception_29-intellect64_exception_offsets
    .long intellect64_exception_30-intellect64_exception_offsets
    .long intellect64_exception_31-intellect64_exception_offsets
.size intellect64_exception_offsets, .-intellect64_exception_offsets
.section .note.GNU-stack,"",@progbits
