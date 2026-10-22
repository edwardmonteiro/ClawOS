ENTRY(_start)
SECTIONS
{
  . = 0x10000;
  .text : { *(.text.boot) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  . = ALIGN(16);
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  __bss_end = .;
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
