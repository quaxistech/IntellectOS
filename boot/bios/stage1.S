/* BIOS stage1, physical 0x8000, GNU as real-mode syntax.
 * Uses BIOS only before switching to protected mode.
 * Reads 64 KiB of kernel image from fixed raw-disk LBA 17..144.
 * All disk operations are READ ONLY. USB-HDD Legacy BIOS is required.
 */
.code16
.section .text
.global _start
_start:
    cli
    ljmp $0x0000,$real_entry
real_entry:
    xorw %ax,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movw $0x7c00,%sp
    cld
    sti
    call serial_init
    movw $hello,%si
    call print

    /* BootInfo ABI v1 at 0x5000; E820 array at 0x6000.
     * The kernel will not use BIOS interrupts after the handoff.
     * Keep offsets in sync with kernel/include/bootinfo.h. */
    xorw %ax,%ax
    movw %ax,%es
    movw $0x5000,%di
    movw $76,%cx
    rep stosw                         /* clear 152-byte header */
    movl $0x49534f33,0x5000         /* magic */
    movw $1,0x5004                 /* ABI version */
    movw $152,0x5006               /* header size */
    movl $152,0x5008               /* total header size */
    movl $1,0x500c                 /* boot method: BIOS */
    movzbl 0x0500,%eax
    movl %eax,0x5014              /* boot drive */
    movl $1,0x5020                 /* video kind: VGA text */
    movb $0x0f,%ah
    int $0x10
    movzbl %al,%eax
    movl %eax,0x5024              /* BIOS video mode */
    movl $0x6000,0x5048           /* memory map physical address */
    movl $24,0x5054               /* memory entry stride */

    movw $e820_msg,%si
    call print
    xorl %ebx,%ebx
    movw $0x6000,%bp
    xorw %si,%si                   /* entry count */
e820_next:
    cmpw $64,%si
    jae e820_capped
    xorw %ax,%ax
    movw %ax,%es
    movw %bp,%di
    movl $1,%es:20(%di)           /* older BIOS may return only 20 bytes */
    movl $0xe820,%eax
    movl $0x534d4150,%edx
    movl $24,%ecx
    pushw %si
    pushw %bp
    pushw %di
    int $0x15
    popw %di
    popw %bp
    popw %si
    jc e820_done
    cmpl $0x534d4150,%eax
    jne e820_done
    cmpw $20,%cx
    jb e820_done
    movl %es:8(%di),%eax
    orl %es:12(%di),%eax
    jz e820_continue              /* skip length = 0 */
    cmpw $24,%cx
    jb e820_record
    testl $1,%es:20(%di)
    jz e820_continue              /* skip invalid extended entry */
e820_record:
    /* E820 1..5 already match the cross-firmware ABI classes.
     * Unknown BIOS-specific types become RESERVED (=2). */
    movl %es:16(%di),%eax
    cmpl $1,%eax
    jb e820_reserved
    cmpl $5,%eax
    jbe e820_count
 e820_reserved:
    movl $2,%es:16(%di)
 e820_count:
    incw %si
    addw $24,%bp
e820_continue:
    testl %ebx,%ebx
    jnz e820_next
    jmp e820_done
e820_capped:
    testl %ebx,%ebx
    jz e820_done
    orl $2,0x5010                /* truncated memory map */
e820_done:
    movzwl %si,%eax
    movl %eax,0x5050             /* map count */
    testw %si,%si
    jz no_memory_map
    orl $1,0x5010                /* valid memory map */
    movw %si,%ax
    call hex16
    call newline

    /* Firmware discovery returns only checked physical entry-point addresses.
     * A missing table is not a boot error; pointer stays zero. */
    call discover_firmware

    /* BIOS EDD extended read. 32 sectors per request, four requests.
     * 0x1000:0, 0x1400:0, 0x1800:0 and 0x1c00:0. */
    movw $loading,%si
    call print
    movw $0x1000,%bp
    movw $4,%cx
load_next:
    pushw %cx
    pushw %bp
    movw %bp,(dap_segment)
    movb 0x0500,%dl
    movw $dap,%si
    movb $0x42,%ah
    int $0x13
    jc disk_error
    popw %bp
    addw $0x0400,%bp
    addl $32,(dap_lba)
    popw %cx
    loop load_next

    movw $loaded,%si
    call print
    cli
    /* Enable A20 using the PC fast A20 gate (port 0x92). */
    inb $0x92,%al
    andb $0xfe,%al
    orb $0x02,%al
    outb %al,$0x92

    lgdt gdt_desc
    movl %cr0,%eax
    orl $1,%eax
    movl %eax,%cr0
    ljmpl $0x08,$protected_entry

no_memory_map:
    movw $memory_failed,%si
    call print
    jmp halt16

disk_error:
    popw %bp
    popw %cx
    movw $failed,%si
    call print
halt16:
    cli
1:  hlt
    jmp 1b

.code32
protected_entry:
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    movw %ax,%ss
    movl $0x70000,%esp
    movl $0x10000,%eax
    jmp *%eax

.code16
newline:
    pushw %ax
    movb $13,%al
    call putc
    movb $10,%al
    call putc
    popw %ax
    ret
print:
    pushw %ax
    pushw %si
1:  lodsb
    testb %al,%al
    jz 2f
    call putc
    jmp 1b
2:  popw %si
    popw %ax
    ret
hex16:
    pushw %ax
    movb %ah,%al
    call hex8
    popw %ax
    call hex8
    ret
hex8:
    pushw %ax
    movb %al,%ah
    shrb $4,%al
    call hex_digit
    movb %ah,%al
    andb $15,%al
    call hex_digit
    popw %ax
    ret
hex_digit:
    andb $15,%al
    cmpb $9,%al
    jbe 1f
    addb $7,%al
1:  addb $'0',%al
    jmp putc
putc:
    pushw %ax
    pushw %bx
    movb $0x0e,%ah
    movw $0x000f,%bx
    int $0x10
    popw %bx
    popw %ax
    cmpb $0,(uart_ok)
    je 2f
    pushw %ax
    pushw %cx
    pushw %dx
    movb %al,%ah
    movw $0x3fd,%dx
    movw $512,%cx
1:  inb %dx,%al
    testb $0x20,%al
    jnz 3f
    loop 1b
    jmp 4f
3:  movw $0x3f8,%dx
    movb %ah,%al
    outb %al,%dx
4:  popw %dx
    popw %cx
    popw %ax
2:  ret
serial_init:
    movw $0x3ff,%dx
    movb $0x5a,%al
    outb %al,%dx
    inb %dx,%al
    cmpb $0x5a,%al
    jne 1f
    movw $0x3f9,%dx
    xorb %al,%al
    outb %al,%dx
    movw $0x3fb,%dx
    movb $0x80,%al
    outb %al,%dx
    movw $0x3f8,%dx
    movb $1,%al
    outb %al,%dx
    movw $0x3f9,%dx
    xorb %al,%al
    outb %al,%dx
    movw $0x3fb,%dx
    movb $3,%al
    outb %al,%dx
    movw $0x3fc,%dx
    movb $0x0b,%al
    outb %al,%dx
    movb $1,(uart_ok)
1:  ret

/* Read-only BIOS firmware entry-point searches. No ACPI method execution. */
discover_firmware:
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %si
    pushw %di
    pushw %es
    /* EBDA first 1 KiB, base segment from BIOS Data Area. */
    movw 0x040e,%ax
    testw %ax,%ax
    jz rsdp_rom
    movw %ax,%es
    xorw %di,%di
    movw $63,%cx                 /* avoid crossing segment at 1024 boundary */
    call rsdp_scan
    testl %eax,%eax
    jnz rsdp_found
rsdp_rom:
    movw $0xe000,%ax
    movw %ax,%es
    xorw %di,%di
    movw $4094,%cx              /* avoid segment wrap during checksum */
    call rsdp_scan
    testl %eax,%eax
    jnz rsdp_found
    movw $0xf000,%ax
    movw %ax,%es
    xorw %di,%di
    movw $4094,%cx
    call rsdp_scan
    testl %eax,%eax
    jz smbios_begin
rsdp_found:
    movl %eax,0x5058             /* ACPI RSDP address */
    orl $4,0x5010
smbios_begin:
    movw $0xf000,%ax
    movw %ax,%es
    xorw %di,%di
    movw $4092,%cx              /* room for SMBIOS EP checksum */
smbios_next:
    cmpl $0x5f4d535f,%es:(%di)  /* ASCII "_SM_" */
    jne smbios_advance
    movzbw %es:5(%di),%dx       /* SMBIOS entry length */
    cmpw $0x1f,%dx
    jb smbios_advance
    cmpw $0x40,%dx
    ja smbios_advance
    pushw %cx
    pushw %di
    movw %dx,%cx
    xorb %bl,%bl
smbios_sum:
    addb %es:(%di),%bl
    incw %di
    loop smbios_sum
    popw %di
    popw %cx
    testb %bl,%bl
    jnz smbios_advance
    movw %es,%ax
    movzwl %ax,%eax
    shll $4,%eax
    movzwl %di,%edx
    addl %edx,%eax
    movl %eax,0x5060             /* SMBIOS entry address */
    orl $8,0x5010
    jmp discover_done
smbios_advance:
    addw $16,%di
    loop smbios_next
discover_done:
    popw %es
    popw %di
    popw %si
    popw %dx
    popw %cx
    popw %bx
    popw %ax
    ret

/* Scan ES:DI on 16-byte boundaries; CX is max candidates; returns EAX =
 * physical RSDP address, or zero. Preserves DI/CX for caller. */
rsdp_scan:
    pushw %bx
    pushw %cx
    pushw %di
rsdp_next:
    cmpl $0x20445352,%es:(%di)  /* "RSD " */
    jne rsdp_advance
    cmpl $0x20525450,%es:4(%di) /* "PTR " */
    jne rsdp_advance
    pushw %cx
    pushw %di
    movw $20,%cx
    xorb %bl,%bl
rsdp_sum:
    addb %es:(%di),%bl
    incw %di
    loop rsdp_sum
    popw %di
    popw %cx
    testb %bl,%bl
    jnz rsdp_advance
    movw %es,%ax
    movzwl %ax,%eax
    shll $4,%eax
    movzwl %di,%ebx
    addl %ebx,%eax
    jmp rsdp_exit
rsdp_advance:
    addw $16,%di
    loop rsdp_next
    xorl %eax,%eax
rsdp_exit:
    popw %di
    popw %cx
    popw %bx
    ret

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff  /* flat 32-bit executable */
    .quad 0x00cf92000000ffff  /* flat 32-bit data */
gdt_end:
gdt_desc:
    .word gdt_end-gdt-1
    .long gdt

.align 4
dap:
    .byte 16,0
    .word 32
dap_offset: .word 0
dap_segment: .word 0x1000
dap_lba: .quad 17

uart_ok: .byte 0
hello:    .asciz "\r\nINTELLECT BOOT 0.4 - BIOS LOADER\r\n"
e820_msg: .asciz "E820 entries: 0x"
loading:  .asciz "Loading native kernel (read-only)...\r\n"
loaded:   .asciz "Switching CPU to 32-bit protected mode...\r\n"
failed:   .asciz "FATAL: BIOS disk read failed\r\n"
memory_failed: .asciz "FATAL: BIOS E820 memory map unavailable\r\n"
