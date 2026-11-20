/* Position-independent flat x86-64 executable. No dynamic relocations. */
ENTRY(intellect64_entry)
SECTIONS
{
  . = 0;
  .text : { KEEP(*(.text.entry)) *(.text .text.*) }
  . = ALIGN(0x1000);
  __intellect_text_end = .;
  .rodata : { *(.rodata .rodata.*) }
  . = ALIGN(0x1000);
  __intellect_rodata_end = .;
  .data : { *(.data .data.*) }
  . = ALIGN(0x1000);
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
  . = ALIGN(0x1000);
  __intellect_bss_end = .;
  __kernel_image_end = .;
  ASSERT(__kernel_image_end <= 0x80000, "kernel runtime exceeds 128 reserved pages")
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) *(.llvm_addrsig*) }
}
