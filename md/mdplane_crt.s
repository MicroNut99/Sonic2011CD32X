| PORT32X: round S3b - start-up of the Genesis demo program (MDPLANE.BIN).
| The listener's 'M' command jumps here (Word RAM, Genesis side 0x200000).
| Interrupts off, stack below the BIOS jump table, .data copied to work RAM, .bss cleared.

        .section .text.start,"ax"
        .global _start
_start:
        move.w  #0x2700,sr
        lea     0xFFFC00,sp
        lea     __data_load,a0
        lea     __data_start,a1
        lea     __data_end,a2
1:
        cmpa.l  a2,a1
        bhs.b   2f
        move.l  (a0)+,(a1)+
        bra.b   1b
2:
        lea     __bss_start,a1
        lea     __bss_end,a2
3:
        cmpa.l  a2,a1
        bhs.b   4f
        clr.l   (a1)+
        bra.b   3b
4:
        jsr     main
5:
        bra.b   5b

| level 6 (vertical blank): count frames - main() waits on this counter
        .text
        .global vint_handler
vint_handler:
        addq.l  #1,vint_count
        rte

        .data
        .align  2
        .global vint_count
vint_count:
        .long   0

