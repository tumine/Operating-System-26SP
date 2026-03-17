OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /* QEMU virt board RAM starts at 0x80000000 */
  . = 0x80000000;

  .text : {
    *(.text.entry)
    *(EXCLUDE_FILE (*trampoline.o) .text)
    *(EXCLUDE_FILE (*trampoline.o) .text.*)
    . = ALIGN(0x1000);
    trampoline = .;
    *(.text.trampoline)
    . = ALIGN(0x1000);
  }
  PROVIDE(etext = .);

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* small read-only data */
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* small data */
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* small bss */
    *(.bss .bss.*)
    *(COMMON)
  }

  PROVIDE(end = .);
}
