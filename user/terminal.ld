ENTRY(_start)
SECTIONS {
  . = 0;
  .text : { KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) }
  . = ALIGN(16);
  __u1_end = .;
  ASSERT(__u1_end <= 0x1000, "bootstrap terminal exceeds one code page")
  ASSERT(SIZEOF(.data) == 0, "bootstrap terminal .data forbidden")
  ASSERT(SIZEOF(.bss) == 0, "bootstrap terminal .bss forbidden")
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) *(.llvm_addrsig*) }
}
