/* PORT32X S7b: the engine image - code and read-only data in the cart (IMAGE.RAW at cart offset 0,
   slots 0-3 = 512 KB), initial data copied into SDRAM, zeroed arrays in SDRAM, the heap after them.
   SDRAM: 0x06000000-0x06002000 Kobo's start-up + the boot loader (its boot record at 0x06001F00),
   0x06002000- the engine's data, zeroed arrays, heap (8 KB reserved); 0x0603C400-0x0603F400 the master
   stack (12 KB) (S7b3: 16 KB heap + 16 KB stack did not fit). R1W: 0x0603D400-0x0603F400 (8 KB) - the
   engine's deepest stack is 3.9 KB (sh2emu "stack low 0603E4C8": start-up, stage load, play); the 4 KB
   above it hold the code that runs every frame (.sdtext). sh2emu checks the 8 KB (STACK, W-GAP).
   sh-elf adds '_' to C names: _scd_x here is scd_x in C. */
OUTPUT_ARCH(sh)
ENTRY(_scd_engine_entry)
MEMORY {
    cart  (rx)  : ORIGIN = 0x02000000, LENGTH = 0x00080000
    sdram (rwx) : ORIGIN = 0x06002000, LENGTH = 0x0003B400 /* R1W: to 0x0603D400 (the stack's 8 KB above) */
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
        /* R1V: not the library helpers .sdtext takes (below) */
        *(EXCLUDE_FILE(*libgcc.a:_sdivsi3.o *libgcc.a:_lshiftrt.o *libgcc.a:_ashiftlt.o *libgcc.a:_ashiftrt.o
                       *libgcc.a:_ashiftrt_n.o) .text)
        *(.text.* .gnu.linkonce.t.*)
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
    /* R1V: code the SH2 runs every frame (SCD_HOT in the engine, SCD_MD_HOT in md/), in SDRAM: scd_engine_entry
       copies it from the cart like .data. A code cache miss costs 11 cycles here, 87 in the cart. Also the
       library's division and shift helpers the scripts and the collision code call every frame. */
    .sdtext : ALIGN(16) {
        _scd_sdtext_start = .;      /* the section's first byte: its cart copy starts at _scd_sdtext_load */
        *(.sdtext .sdtext.*)
        *libgcc.a:_sdivsi3.o(.text)
        *libgcc.a:_lshiftrt.o(.text)
        *libgcc.a:_ashiftlt.o(.text)
        *libgcc.a:_ashiftrt.o(.text)
        *libgcc.a:_ashiftrt_n.o(.text)
        . = ALIGN(16);
        _scd_sdtext_end = .;
    } > sdram AT > cart
    _scd_sdtext_load = LOADADDR(.sdtext);
    .bss (NOLOAD) : {
        _scd_bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(8);
        _scd_bss_end = .;
    } > sdram
    end = .;
    _end = .;
    ASSERT(_end <= 0x0603D400, "engine RAM too big: data + zeroed arrays reach the stack (S7e: no heap any more; R1W: 8 KB stack)")
    ASSERT(_scd_data_load + (_scd_data_end - _scd_data_start) <= 0x02080000, "engine image over 512 KB (cart slots 0-3)")
    ASSERT(_scd_sdtext_load + (_scd_sdtext_end - _scd_sdtext_start) <= 0x02080000, "engine image over 512 KB (cart slots 0-3)")
    ASSERT(_scd_sdtext_load >= _scd_data_load + (_scd_data_end - _scd_data_start), "R1V: .sdtext's cart copy overlaps .data's")
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr .comment .note*) }
}
