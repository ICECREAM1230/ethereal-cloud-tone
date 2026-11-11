OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
  ROM (rx) : ORIGIN = 0x80000000, LENGTH = 64K
  /* Hardware DTCM begins at zero. Reserve its first 256 bytes so a static
   * C object can never acquire NULL as its address after a link-order change.
   * The control/parser/voice APIs intentionally reject NULL pointers. */
  RAM (rw) : ORIGIN = 0x00000100, LENGTH = 64K - 256
}
SECTIONS {
  .text : { KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); } > ROM
  .data : { __data_start = .; *(.data .data.* .sdata .sdata.*) . = ALIGN(4); __data_end = .; } > RAM AT> ROM
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); __bss_end = .; } > RAM
  __global_pointer$ = ORIGIN(RAM) + 0x800;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  ASSERT(__bss_end < __stack_top - 4096, "DTCM stack reserve exceeded")
  /DISCARD/ : { *(.comment .note.* .eh_frame*) }
}
