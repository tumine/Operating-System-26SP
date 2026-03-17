OUTPUT_ARCH("riscv")
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5); /* R=4, X=1 */
  data PT_LOAD FLAGS(6); /* R=4, W=2 */
}

SECTIONS
{
  . = 0x0;

  .text : {
    *(.text.entry)
    *(.text .text.*)
  } :text

  .rodata : {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
  } :text

  /* Page-align the start of the RW segment. */
  . = ALIGN(0x1000);

  .data : {
    *(.sdata .sdata.*)
    *(.data .data.*)
  } :data

  .bss : {
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
  } :data

  PROVIDE(end = .);
}
