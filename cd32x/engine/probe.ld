/* PORT32X: S5a - LINK PROBE ONLY (not a runnable layout). Code and read-only data in the cart
   window (0x02000000, 4 MB), initialised data in SDRAM, and everything else in an oversized
   pseudo-region so the link succeeds and the map shows the real size of every array.
   The runnable layout is decided from these numbers (S5b). */
OUTPUT_ARCH(sh)
ENTRY(_main)
MEMORY {
    cart  (rx) : ORIGIN = 0x02000000, LENGTH = 0x00400000
    sdram (rw) : ORIGIN = 0x06000000, LENGTH = 0x00040000
    huge  (rw) : ORIGIN = 0x10000000, LENGTH = 0x04000000
}
SECTIONS {
    .text : { *(.text .text.*) *(.gnu.linkonce.t.*) } > cart
    .rodata : { *(.rodata .rodata.*) *(.gnu.linkonce.r.*) } > cart
    .init_array : { __init_array_start = .; KEEP(*(.init_array*)) KEEP(*(.ctors*)) __init_array_end = .; } > cart
    .fini_array : { KEEP(*(.fini_array*)) KEEP(*(.dtors*)) } > cart
    .eh_frame : { KEEP(*(.eh_frame)) } > cart
    .data : { *(.data .data.*) *(.sdata .sdata.*) } > sdram AT > cart
    .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) __bss_end = .; } > huge
    . = ALIGN(8);
    end = .;
    _end = .;
}
