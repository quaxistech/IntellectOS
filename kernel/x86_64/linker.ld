/* Position-independent flat x86-64 executable. No dynamic relocations. */
ENTRY(intellect64_entry)
SECTIONS
{
  . = 0;
  .text : { KEEP(*(.text.entry)) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
  __kernel_image_end = .;
  ASSERT(__kernel_image_end <= 0x80000, "kernel runtime exceeds 128 reserved pages")
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) *(.llvm_addrsig*) }
}
