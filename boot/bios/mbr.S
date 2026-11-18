/* Intellect Boot 0.4: tiny BIOS MBR/VBR loader. GNU as, 16-bit real mode.
 * BIOS jumps to 0000:7C00 or 07C0:0000; normalize CS before using absolute labels.
 * Sector is simultaneously valid as MBR bootstrap and FAT16 VBR boot code.
 */
.code16
.section .text
.global _start
_start:
    jmp boot_start
    nop
    .ascii "INTELCT "            /* OEM name, 8 bytes */
    .word 512                 /* FAT16 BPB: bytes/sector */
    .byte 1                   /* sectors/cluster */
    .word 17                  /* reserved sectors: boot + 16 mirrored stage1 sectors */
    .byte 2                   /* FAT count */
    .word 512                 /* root entries */
    .word 32768               /* total sectors */
    .byte 0xf8                /* fixed medium */
    .word 128                 /* FAT size in sectors */
    .word 63                  /* sectors/track hint */
    .word 255                 /* heads hint */
    .long 2048                /* VBR hidden sectors */
    .long 0                   /* large sector count */
    .byte 0x80                /* drive number */
    .byte 0                   /* reserved */
    .byte 0x29                /* extended boot signature */
    .long 0x49303242          /* volume serial */
    .ascii "INTELLECTOS"      /* label, 11 bytes */
    .ascii "FAT16   "         /* filesystem type string */
    .org 62
boot_start:
    cli
    ljmp $0x0000, $real_entry
real_entry:
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    sti
    movb %dl, (boot_drive)
    movb %dl, 0x0500          /* handoff for stage1 */

    movw $0x0003, %ax        /* BIOS VGA 80x25 mode; no GRUB console */
    int $0x10
    movw $banner, %si
    call print

    /* BIOS INT 13h Extensions: EDD support and LBA reads */
    movb (boot_drive), %dl
    movw $0x55aa, %bx
    movb $0x41, %ah
    int $0x13
    jc no_edd
    cmpw $0xaa55, %bx
    jne no_edd
    testb $0x01, %cl
    jz no_edd

    movw $edd_ok, %si
    call print
    movb (boot_drive), %dl
    movw $dap, %si
    movb $0x42, %ah
    int $0x13
    jc read_failed
    movw $loaded, %si
    call print
    movb (boot_drive), %dl
    ljmp $0x0000, $0x8000

no_edd:
    movw $edd_error, %si
    call print
    jmp hang
read_failed:
    movw $read_error, %si
    call print
hang:
    cli
1:  hlt
    jmp 1b

print:
    pushw %ax
    pushw %bx
    pushw %si
1:  lodsb
    testb %al, %al
    jz 2f
    movb $0x0e, %ah
    movw $0x000f, %bx
    int $0x10
    jmp 1b
2:  popw %si
    popw %bx
    popw %ax
    ret

boot_drive: .byte 0
banner:    .asciz "INTELLECT BOOT 0.4 - OUR OWN BIOS LOADER\r\n"
edd_ok:    .asciz "INT13 EDD: OK\r\n"
loaded:    .asciz "STAGE1: LOADED; TRANSFERRING CONTROL\r\n"
edd_error: .asciz "ERROR: BIOS INT13 EDD NOT SUPPORTED\r\n"
read_error:.asciz "ERROR: COULD NOT READ STAGE1 FROM USB\r\n"
.align 4
dap:
    .byte 16, 0
    .word 16                  /* sectors, 8 KiB */
    .word 0x8000              /* destination offset */
    .word 0x0000              /* destination segment */
    .quad 1                   /* absolute LBA 1, right after MBR */

/* Real MBR partition table; active FAT16 at LBA2048, 16 MiB */
.org 446
    .byte 0x80,0x20,0x21,0x00  /* active, CHS hint */
    .byte 0x06,0xfe,0xff,0xff  /* FAT16, ending CHS saturated */
    .long 2048,32768
    .fill 48, 1, 0
    .word 0xaa55
