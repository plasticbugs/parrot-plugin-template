/* tools/armcount/bench.ld: everything in the RP2350's SRAM, as a plugin is.
   (GCC 9 puts a global without static and without a value in COMMON:
   it belongs in .bss too.) */
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K }
ENTRY(_start)
SECTIONS {
  .text : { *(.text._start) *(.text*) *(.time_critical*) *(.rodata*) } > RAM
  .data : { *(.data*) } > RAM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; } > RAM
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
