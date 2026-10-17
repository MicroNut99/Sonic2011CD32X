/* REV R4: a scene's native scripts (tools/scd_aot.py --part stage) as a blob in its cart image (part 18): linked for
   that part's place in the cart (--section-start=.blob=ADDRESS) against the engine's symbols (--just-symbols); code and
   tables only - it runs from the cart, it has no data of its own. Its first bytes: scdNativeBlob (engine/Script.hpp). */
OUTPUT_ARCH(sh)
SECTIONS {
    .blob : {
        KEEP(*(.blob_header))
        *(.text .text.* .sdtext .sdtext.* .gnu.linkonce.t.*)
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        . = ALIGN(4);
    }
    .data : { *(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON) }
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr .comment .note* .init_array* .fini_array* .ctors* .dtors* .jcr) }
}
ASSERT(SIZEOF(.data) == 0, "a native scripts blob must not have data of its own (it runs from the cart)")
