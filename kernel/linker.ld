OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5); /* RX */
  data PT_LOAD FLAGS(6); /* RW */
}
SECTIONS
{
  . = 0x10000;
  .text : { KEEP(*(.text.start)) *(.text*) } :text
  .rodata ALIGN(16) : { *(.rodata*) } :text
  .data ALIGN(16) : { *(.data*) } :data
  __image_end = .;
  .bss ALIGN(16) (NOLOAD) : {
    __bss_start = .;
    *(COMMON)
    *(.bss*)
    __bss_end = .;
  } :data
  /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) *(.gnu*) }
}
