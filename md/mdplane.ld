/* PORT32X: round S3b - MDPLANE.BIN: the program runs from Word RAM (Genesis side 0x200000,
   128 KB in 1M mode); its variables live in Genesis work RAM 0xFF8000-0xFFDFFF (the listener
   used 0xFF1000.., the BIOS uses 0xFFFD00..); the stack is below 0xFFFC00. The stage data is
   appended after the program at __blob_start (tools/make_mdplane.py). */
OUTPUT_ARCH(m68k)
ENTRY(_start)
MEMORY {
    wram (rx) : ORIGIN = 0x200000, LENGTH = 0x20000
    ram  (rw) : ORIGIN = 0xFF8000, LENGTH = 0x6000
}
SECTIONS {
    .text 0x200000 : {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > wram
    .data : {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > ram AT > wram
    __data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram
    __blob_start = ALIGN(LOADADDR(.data) + SIZEOF(.data), 16);
}
