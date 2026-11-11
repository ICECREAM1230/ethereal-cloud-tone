OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
  ROM (rx) : ORIGIN = 0x80000000, LENGTH = 64K
  RAM (rw) : ORIGIN = 0x00000000, LENGTH = 64K
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
