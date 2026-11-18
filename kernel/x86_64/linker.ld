/* Position-independent flat x86-64 executable. No dynamic relocations. */
ENTRY(intellect64_entry)
SECTIONS
{
  . = 0;
  .text : { KEEP(*(.text.entry)) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) *(.llvm_addrsig*) }
}
