/* PORT32X S7b: the engine image - code and read-only data in the cart (IMAGE.RAW at cart offset 0,
   slots 0-3 = 512 KB), initial data copied into SDRAM, zeroed arrays in SDRAM, the heap after them.
   SDRAM: 0x06000000-0x06002000 Kobo's start-up + the boot loader (its boot record at 0x06001F00),
   0x06002000- the engine's data, zeroed arrays, heap (8 KB reserved); 0x0603C400-0x0603F400 the master
   stack (12 KB) (S7b3: 16 KB heap + 16 KB stack did not fit).
   sh-elf adds '_' to C names: _scd_x here is scd_x in C. */
OUTPUT_ARCH(sh)
ENTRY(_scd_engine_entry)
MEMORY {
    cart  (rx)  : ORIGIN = 0x02000000, LENGTH = 0x00080000
    sdram (rwx) : ORIGIN = 0x06002000, LENGTH = 0x0003A400
}
SECTIONS {
    /* S7c2: FIRST rule - an input section goes to the first rule that matches, so this drops
       libstdc++ eh_alloc.o's static constructor before .init_array below can take it. It reserved
       the emergency exception pool with malloc (console: "NO HEAP A0900", caller
       _GLOBAL__sub_I__ZN9__gnu_cxx9__freeresEv). The engine has no exceptions. */
    /DISCARD/ : { *eh_alloc.o(.init_array .init_array.* .ctors .ctors.*) }
    .text : {
        KEEP(*(.text.entry))
        *eng/*.o(.text .text.* .gnu.linkonce.t.*)
        _scd_engine_code_end = .;   /* S7b6: the engine's own code ends here; the libraries follow */
        *(.text .text.* .gnu.linkonce.t.*)
    } > cart
    .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) } > cart
    .init_array : {
        _scd_init_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(.ctors))
        _scd_init_end = .;
    } > cart
    .data : {
        _scd_data_start = .;
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        _scd_data_end = .;
    } > sdram AT > cart
    _scd_data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        _scd_bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(8);
        _scd_bss_end = .;
    } > sdram
    end = .;
    _end = .;
    ASSERT(_end <= 0x0603C400, "engine RAM too big: data + zeroed arrays reach the stack (S7e: no heap any more)")
    ASSERT(_scd_data_load + (_scd_data_end - _scd_data_start) <= 0x02080000, "engine image over 512 KB (cart slots 0-3)")
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr .comment .note*) }
}
