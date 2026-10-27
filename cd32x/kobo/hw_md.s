| SEGA MegaCD support code
| by Chilly Willy

| MD Hardware Calls - keep in sync with the enums in hw_md.h

        .equ    MD_CMD_INIT_HW, 1
        .equ    MD_CMD_SET_SR, 2
        .equ    MD_CMD_GET_PAD, 3
        .equ    MD_CMD_CLEAR_B, 4
        .equ    MD_CMD_SET_VRAM, 5
        .equ    MD_CMD_NEXT_VRAM, 6
        .equ    MD_CMD_COPY_VRAM, 7
        .equ    MD_CMD_CLEAR_A, 8
        .equ    MD_CMD_PUT_STR, 9
        .equ    MD_CMD_PUT_CHR, 10
        .equ    MD_CMD_DELAY, 11
        .equ    MD_CMD_SET_PALETTE, 12
        .equ    MD_CMD_Z80_BUSREQUEST, 13
        .equ    MD_CMD_Z80_RESET, 14
        .equ    MD_CMD_Z80_MEMCLR, 15
        .equ    MD_CMD_Z80_MEMCPY, 16
        .equ    MD_CMD_DMA_SCREEN, 17
        .equ    MD_CMD_INIT_32X, 18
        .equ    MD_CMD_COPY_FROM_CART, 24
        .equ    MD_CMD_COPY_WORDS, 25
        .equ    MD_CMD_SAFE_CART_XFER, 26
        .equ    MD_CMD_END, 35              /* KOBO K19: + set_vdp_reg (34) */

| SONIC REV R6 - MODE1: the listener for a CARTRIDGE boot (the Sega CD's "mode 1"), which is how a disc
| is started with a Mega EverDrive PRO in the slot: the EverDrive cannot let go of the cartridge line
| (CD32X-BootCamp docs/12, console 25 Oct 2026), so EDCD_SONICCD.32X does the BIOS's and the boot block's
| work and hands over here with the console still in cartridge mode.  Three things change, all of them
| inside this file and all of them only when SCD_MODE1 is 1 (without it every byte below assembles
| exactly as before - checked):
|   MODE1-1  every address argument moves to the cartridge-boot map: Word RAM 0x200000-0x3FFFFF ->
|            0x600000-0x7FFFFF, the cart 0x400000-0x7FFFFF -> 0x000000-0x3FFFFF.  Commands 19-22 (32X
|            register reads and writes) are left alone: their arguments are values, not addresses.
|            The hand-over jumps to Word RAM at 0x600000.
|   MODE1-3  a copy to or from the cart goes through a 1 KB buffer in work RAM: the Sega CD side with
|            RV = 0 (a Word RAM read made with RV = 1 comes out garbled in cartridge mode - console),
|            the cart side with RV = 1 and interrupts off.  The EverDrive's window 0 (cart 0x000000-
|            0x07FFFF) cannot be written by the 68000 at all with the 32X on, so a copy there is made
|            through window 1 with window 0's bank in it.
|   MODE1-6  the EverDrive's write-enable bit (CTRL0 bit 13) is ON only inside a copy INTO the cart:
|            with it left on, the 68000's other writes corrupt the cart (console, 25 Oct 2026, edw0).
        .ifndef SCD_MODE1
        .equ    SCD_MODE1, 0
        .endif
        .ifndef SCD_M1CTRL0
        .equ    SCD_M1CTRL0, 0x8000        /* CTRL0 as EDCD leaves it: P on, W off */
        .endif
        .equ    SCD_M1CTRL0_W, SCD_M1CTRL0 + 0x2000   /* + not |: | starts a comment here */

| SONIC 2011 REV R1 - the Genesis planes (md/sgb.h): work RAM above the listener. The Sub-CPU
| sets them up with the listener's own commands (write_32x_16 / copy_vram / set_palette /
| set_vdp_reg: SH2 command 68, cd32x/sub_main.c); sgb_poll (command loop) and sgb_vblank
| (vertical blank) do the rest. Nothing here runs while SGB_FLAG is 0 - Kobo's listener as before.
        .equ    SGB_FLAG,  0xFF8000         /* w: 1 = the planes are on (records expected) */
        .equ    SGB_LAST,  0xFF8002         /* w: id of the last record taken (COMM4 / COMM6) */
        .equ    SGB_READY, 0xFF8004         /* w: 1 = a record waits in SGB_REC for vertical blank */
        .equ    SGB_BAD,   0xFF8006         /* w: R1G - 1 = a record id came with no valid record behind it */
        .equ    SGB_REC,   0xFF8010         /* the record: length word + up to SGB_RECMAX words */
        .equ    SGB_RECMAX,6000
| SONIC REV R4D - the underwater colours (md/sgb.h: the records' water trailer; the Options Menu's UNDERWATER switch
| on - off, no record has the trailer and nothing below touches the VDP). Vertical blank: the normal colours back into
| CRAM (if the water ones went in), then this frame's H-int at the water line; the H-int writes the 64 water colours.
| The VDP only while SGB_FLAG is 1 (the command loop does not use it then): a register written in the middle of
| another routine's VDP address and data would send its data elsewhere.
        .equ    SGB_HON,   0xFF8008         /* w: 1 = the H-int is set up for the water line (IE1 on) */
        .equ    SGB_WON,   0xFF800A         /* w: 1 = CRAM holds the water colours (this frame's H-int wrote them) */
        .equ    SGB_WLINE, 0xFF800C         /* w: the first line with the water colours (0: none) */
        .equ    SGB_NCRAM, 0xFFB000         /* 64 w: the normal colours (every palette change, a shadow of CRAM) */
        .equ    SGB_WCRAM, 0xFFB080         /* 64 w: the water colours (the trailers' changes) */
        .equ    SGB_WTAG,  0x5741           /* the trailer's last word (md/sgb.h SGB_WATER_TAG) */


        .text

        .align  2

| int do_md_cmd0(int cmd);
        .global do_md_cmd0
do_md_cmd0:
        move.l  4(sp),d0
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd1(int cmd, int arg1);
        .global do_md_cmd1
do_md_cmd1:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd2(int cmd, int arg1, int arg2);
        .global do_md_cmd2
do_md_cmd2:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd3(int cmd, int arg1, int arg2, int arg3);
        .global do_md_cmd3
do_md_cmd3:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd4(int cmd, int arg1, int arg2, int arg3, int arg4);
        .global do_md_cmd4
do_md_cmd4:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        move.l  20(sp),0x802C.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| void send_md_cmd0(int cmd);
        .global send_md_cmd0
send_md_cmd0:
        move.l  4(sp),d0
        bra.b   send_md_cmd

| void send_md_cmd1(int cmd, int arg1);
        .global send_md_cmd1
send_md_cmd1:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        bra.b   send_md_cmd

| void send_md_cmd2(int cmd, int arg1, int arg2);
        .global send_md_cmd2
send_md_cmd2:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        bra.b   send_md_cmd

| void send_md_cmd3(int cmd, int arg1, int arg2, int arg3);
        .global send_md_cmd3
send_md_cmd3:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        bra.b   send_md_cmd

| void send_md_cmd4(int cmd, int arg1, int arg2, int arg3, int arg4);
        .global send_md_cmd4
send_md_cmd4:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        move.l  20(sp),0x802C.w

send_md_cmd:
        tst.b   0x800E.w
        bne.b   send_md_cmd
        move.b  d0,0x800F.w
0:
        cmp.b   0x800E.w,d0
        bne.b   0b
        cmpi.b  #MD_CMD_DMA_SCREEN,d0
        beq.b   1f
        move.b  #0,0x800F.w
1:
        rts

| int wait_md_cmd(void);
        .global wait_md_cmd
wait_md_cmd:
        move.b  #0,0x800F.w
0:
        tst.b   0x800E.w
        bne.b   0b
        move.l  0x8010.w,d0
        rts

| void switch_banks(void);
| Switch 1M Banks
        .global switch_banks
switch_banks:
        bchg    #0,0x8003.w
0:
        btst    #1,0x8003.w
        bne.b   0b
        rts

| Initialize the SCD hardware & MD code and hardware (called from crt0)
        .global init_hardware
init_hardware:
        lea     md_init_start(pc),a0
        lea     0x080000,a1
        lea     md_init_end(pc),a2
1:
        move.l  (a0)+,(a1)+
        cmpa.l  a0,a2
        bhi.b   1b
        bset    #0,0x8003.w
        move.b  #'M,0x800F.w
2:
        cmpi.b  #'M,0x800E.w
        bne.b   2b
        move.b  #0,0x800F.w
3:
        tst.b   0x800E.w
        bne.b   3b

        bset    #2,0x8003.w
        move    #0x2000,sr
        rts


| Copied to Word RAM for MD to execute
        .align  4
md_init_start:
        move.b  #'M,0xA1200E
        lea     md_start(pc),a0
        lea     0xFF1000,a1
        lea     md_init_end(pc),a2
1:
        move.l  (a0)+,(a1)+
        cmpa.l  a0,a2
        bhi.b   1b
        jmp     0xFF1000

        .align  4
md_start:
        clr.w   SGB_FLAG                /* SONIC REV R1: work RAM is not cleared at power-on - */
        clr.w   SGB_READY               /* the planes stay off until the Sub-CPU loads an act  */
        clr.w   SGB_LAST
        clr.w   SGB_BAD
        clr.w   SGB_HON                 /* SONIC REV R4D: no water line */
        clr.w   SGB_WON
        clr.w   SGB_WLINE
        move.l  #0,0xA12010
        move.l  #0,0xA12014
        move.l  #0,0xA12018
        move.l  #0,0xA1201C
        bsr.w   md_init_hw
        moveq   #0,d7
cmd_done:
        tst.b   0xA1200F
        bne.b   cmd_done
        tst.b   0xA1200F                /* HW10: read twice - a torn read must not end the */
        bne.b   cmd_done                /*       handshake early                            */
        move.b  #0,0xA1200E

        /* main loop */
        moveq   #0,d0
cmd_loop:
        tst.b   d7
        beq.b   md_check
        move.w  0xA15120,d0
        beq.b   md_check

mars_done:
        moveq   #0,d0
        move.w  d0,0xA15120
md_check:
        tst.w   SGB_FLAG                /* SONIC REV R1: a plane record from the SH2? */
        beq.b   md_check_cmd
        bsr.w   sgb_poll
md_check_cmd:
        move.b  0xA1200F,d0
        beq.b   cmd_loop
        cmp.b   0xA1200F,d0             /* HW10: the command byte must read the same twice.  */
        bne.b   md_check                /* WHY (console, 68K EXC V09 SR=A71E, args 0091FFFE): */
                                        /* a read overlapping the Sub-CPU's write returned 2  */
                                        /* instead of 19 -> set_sr(0x0091FFFE): trace on,     */
                                        /* interrupts off; or a silent deadlock (wrong echo). */
        move.b  d0,0xA1200E

        /* push args on stack */
        .if SCD_MODE1
        move.l  0xA1202C,d1             /* MODE1-1: each argument moved to the cartridge-boot map */
        bsr     m1_arg
        move.l  d1,-(sp)
        move.l  0xA12028,d1
        bsr     m1_arg
        move.l  d1,-(sp)
        move.l  0xA12024,d1
        bsr     m1_arg
        move.l  d1,-(sp)
        move.l  0xA12020,d1
        bsr     m1_arg
        move.l  d1,-(sp)
        .else
        move.l  0xA1202C,-(sp)
        move.l  0xA12028,-(sp)
        move.l  0xA12024,-(sp)
        move.l  0xA12020,-(sp)
        .endif

        cmpi.b  #'M,d0
        bne.b   2f
1:
        btst    #0,0xA12003
        beq.b   1b
        .if SCD_MODE1
        jmp     0x600000                /* MODE1-1: Word RAM in a cartridge boot */
        .else
        jmp     0x200000
        .endif
2:
        move.l  #-1,0xA12010
        cmpi.b  #MD_CMD_END,d0
        bhs.b   3f
        add.w   d0,d0
        lea     cmd_table(pc),a0
        move.w  0(a0,d0.w),d0
        jsr     0(a0,d0.w)
        move.l  d0,0xA12010
3:
        lea     16(sp),sp
        bra     cmd_done

cmd_table:
        .word   cmd_done - cmd_table
        .word   md_init_hw - cmd_table
        .word   cmd_nop - cmd_table      /* 2: HW10 set_sr disabled - never sent by the game; a
                                            garbled command 2 must not touch the status register */
        .word   get_pad - cmd_table
        .word   clear_b - cmd_table
        .word   set_vram - cmd_table
        .word   next_vram - cmd_table
        .word   copy_vram - cmd_table
        .word   clear_a - cmd_table
        .word   put_str - cmd_table
        .word   put_chr - cmd_table
        .word   delay - cmd_table
        .word   set_palette - cmd_table
        .word   z80_busrequest - cmd_table
        .word   z80_reset - cmd_table
        .word   z80_memclr - cmd_table
        .word   z80_memcpy - cmd_table
        .word   dma_screen - cmd_table
        .word   init_32x - cmd_table
        .word   read_32x_16 - cmd_table         /* 19 */
        .word   write_32x_16 - cmd_table        /* 20 */
        .word   read_32x_32 - cmd_table         /* 21 */
        .word   write_32x_32 - cmd_table        /* 22 */
        .word   copy_to_cart - cmd_table        /* 23 */
        .word   copy_from_cart - cmd_table      /* 24 */
        .word   copy_words - cmd_table          /* 25 */
        .word   safe_cart_xfer - cmd_table      /* 26 */
        .word   safe_cart_xfer_w - cmd_table    /* 27 */
        .word   bram_put - cmd_table            /* 28 */
        .word   bram_to_fb - cmd_table          /* 29 */
        .word   bram_get - cmd_table            /* 30 */
        .word   install_exc - cmd_table         /* 31 */
        .word   chilly_memcpy - cmd_table       /* 32 */
        .word   chilly_rv_memcpy - cmd_table    /* 33 */
        .word   set_vdp_reg - cmd_table         /* 34  KOBO K19 */

| KOBO K19: void set_vdp_reg(int word);
| Writes one VDP register word (0x8Rvv) to the VDP control port.  Used to switch the
| Genesis display on/off (register 1: 0x8174 = display on, 0x8134 = display off - same
| value md_init_hw uses, only bit 6 "display enable" differs; bit 5 (V-int) stays ON so
| the vblank handler keeps reading the pads).  Runs in the command loop, never inside an
| interrupt, so it cannot split another routine's VDP address write.
set_vdp_reg:
        move.l  4(sp),d0
        move.w  d0,0xC00004
        moveq   #0,d0
        rts

| void md_init_hw(void);
| initialize MD hardware
md_init_hw:
        movem.l d2-d7/a2-a5,-(sp)
        move.w  #0x2700,sr

| init joyports
        lea     0xA10000,a5
        move.b  #0x40,0x09(a5)
        move.b  #0x40,0x0B(a5)
        move.b  #0x40,0x03(a5)
        move.b  #0x40,0x05(a5)

        lea     0xC00000,a3
        lea     0xC00004,a4

| wait on VDP DMA
        move.w  #0x8114,(a4)
0:
        move.w  (a4),d0
        btst    #1,d0
        bne.b   0b

        moveq   #0,d0
        move.w  #0x8000,d5
        move.w  #0x0100,d7

| Set VDP registers
        lea     InitVDPRegs(pc),a5
        moveq   #18,d1
1:
        move.b  (a5)+,d5
        move.w  d5,(a4)
        add.w   d7,d5
        dbra    d1,1b

| clear VRAM
        move.w  #0x8F02,(a4)
        move.l  #0x40000000,(a4)
        move.w  #0x7FFF,d1
2:
        move.w  d0,(a3)
        dbra    d1,2b

| Clear CRAM
        lea     InitVDPRAM(pc),a5
        move.l  (a5)+,(a4)
        move.l  (a5)+,(a4)
        moveq   #31,d3
3:
        move.l  d0,(a3)
        dbra    d3,3b

| Clear VSRAM
        move.l  (a5)+,(a4)
        moveq   #19,d4
4:
        move.l  d0,(a3)
        dbra    d4,4b

| halt Z80 and init FM chip
        move.w  #0x100,0xA11100
        move.w  #0x100,0xA11200

| reset YM2612
        lea     FMReset(pc),a5
        lea     0xA00000,a0
        move.w  #0x4000,d1
        moveq   #26,d2
5:
        move.b  (a5)+,d1
        move.b  (a5)+,0(a0,d1.w)
        nop
        nop
        dbra    d2,5b

        moveq   #0x30,d0
        moveq   #0x5F,d2
6:
        move.b  d0,0x4000(a0)
        nop
        nop
        move.b  #0xFF,0x4001(a0)
        nop
        nop
        move.b  d0,0x4002(a0)
        nop
        nop
        move.b  #0xFF,0x4003(a0)
        nop
        nop
        addq.b  #1,d0
        dbra    d2,6b

| reset PSG
        lea     PSGReset(pc),a5
        lea     0xC00000,a0
        move.b  (a5)+,0x0011(a0)
        move.b  (a5)+,0x0011(a0)
        move.b  (a5)+,0x0011(a0)
        move.b  (a5),0x0011(a0)

| load font tile data
        move.w  #0x8F02,(a4)
        move.l  #0x40000000,(a4)
        lea     font_data(pc),a0
        move.w  #0x6B*8-1,d2
7:
        move.l  (a0)+,d0
        move.l  d0,d1
        not.l   d1
        andi.l  #0x11111111,d0
        andi.l  #0x00000000,d1
        or.l    d1,d0
        move.l  d0,(a3)
        dbra    d2,7b

| set default palette
        move.l  #0xC0000000,(a4)
        move.l  #0x00000CCC,(a3)
        move.l  #0xC0200000,(a4)
        move.l  #0x000000A0,(a3)        /* SONIC CD S3b3: green visible again (diagnosis). HW13 had: 0x00000000
                                           HW13: green text colour (line 1, colour 1, was 0x00A0)
                                           = BLACK - any green line is invisible.  White (line 0)
                                           and the red crash text (line 2) are unchanged. */
        move.l  #0xC0400000,(a4)
        move.l  #0x0000000A,(a3)

        lea     vblank_int(pc),a0
        move.l  a0,0xFFFD08
        move.w  #0x4EF9,0xFFFD0C        /* SONIC REV R4D: the H-int (level 4) - the BIOS jump table's entry: */
        lea     hint_int(pc),a0         /* jmp hint_int (the gate array's H-int vector register, 0xA12006,  */
        move.l  a0,0xFFFD0E             /* points there: 0xFD0C). Off in the VDP (register 0 bit 4) until   */
        move.w  #0xFD0C,0xA12006        /* sgb_water sets a water line                                       */

        move.w  #0x8174,(a4)
        movem.l (sp)+,d2-d7/a2-a5
        move    #0x2000,sr
        rts

InitVDPRegs:
        .byte   0x04, 0x14, 0x30, 0x2C, 0x07, 0x54, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x81, 0x2B, 0x00, 0x01
        .byte   0x01, 0x00, 0x00

        .align  2

InitVDPRAM:
        .word   0x8104, 0x8F01
        .word   0xC000, 0x0000
        .word   0x4000, 0x0010

FMReset:
        .byte   0,0x22, 1,0x00
        .byte   0,0x27, 1,0x00
        .byte   0,0x28, 1,0x00, 1,0x04, 1,0x01, 1,0x05, 1,0x02, 1,0x06
        .byte   0,0x2A, 1,0x80, 0,0x2B, 1,0x00
        .byte   0,0xB4, 1,0x00, 0,0xB5, 1,0x00, 0,0xB6, 1,0x00
        .byte   2,0xB4, 3,0x00, 2,0xB5, 3,0x00, 2,0xB6, 3,0x00

PSGReset:
        .byte   0x9f, 0xbf, 0xdf, 0xff

        .align  4

        .include "font.s"

        .align  4

| HW10: harmless stand-in for commands the game never sends at runtime
cmd_nop:
        moveq   #0,d0
        rts

set_sr:
        moveq   #0,d0
        move.w  sr,d0
        move.l  4(sp),d1
        move.w  d1,sr
        rts

get_pad:
        move.l  d2,-(sp)
        move.l  8(sp),d0
        cmpi.w  #1,d0
        bhi     no_pad
        add.w   d0,d0
        addi.l  #0xA10003,d0
        movea.l d0,a0
        bsr.b   get_input
        move.w  d0,d1
        andi.w  #0x0C00,d0
        bne.b   no_pad
        bsr.b   get_input
        bsr.b   get_input
        move.w  d0,d2
        bsr.b   get_input
        andi.w  #0x0F00,d0
        cmpi.w  #0x0F00,d0
        beq.b   common
        move.w  #0x010F,d2
common:
        lsl.b   #4,d2
        lsl.w   #4,d2
        andi.w  #0x303F,d1
        move.b  d1,d2
        lsr.w   #6,d1
        or.w    d1,d2
        eori.w  #0x1FFF,d2
        move.w  d2,d0
        move.l  (sp)+,d2
        rts

no_pad:
        .ifdef  HAS_SMS_PAD
        move.b  (a0),d0
        andi.w  #0x003F,d0
        eori.w  #0x003F,d0
        .else
        move.w  #0xF000,d0
        .endif
        move.l  (sp)+,d2
        rts

get_input:
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d0
        move.b  #0x40,(a0)
        lsl.w   #8,d0
        move.b  (a0),d0
        rts

clear_b:
        moveq   #0,d0
        lea     0xC00000,a0
        move.w  #0x8F02,4(a0)
        move.l  #0x60000003,d1
        move.l  d1,4(a0)
        move.w  #64*32-1,d1
1:
        move.w  d0,(a0)
        dbra    d1,1b
        rts

set_vram:
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  4(sp),d1
        lsl.l   #2,d1
        lsr.w   #2,d1
        swap    d1
        ori.l   #0x40000000,d1
        move.l  d1,4(a1)
        move.l  8(sp),d0
        move.w  d0,(a1)
        rts

next_vram:
        move.l  4(sp),d0
        move.w  d0,0xC00000
        rts

copy_vram:
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  4(sp),d1
        lsl.l   #2,d1
        lsr.w   #2,d1
        swap    d1
        ori.l   #0x40000000,d1
        move.l  d1,4(a1)
        movea.l 8(sp),a0
        move.l  12(sp),d0
        subq.w  #1,d0
0:
        move.w  (a0)+,(a1)
        dbra    d0,0b
        rts

clear_a:
        moveq   #0,d0
        lea     0xC00000,a0
        move.w  #0x8F02,4(a0)
        move.l  #0x40000003,d1
        move.l  d1,4(a0)
        move.w  #64*32-1,d1
1:
        move.w  d0,(a0)
        dbra    d1,1b
        rts

put_str:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  16(sp),d1
        lsl.l   #6,d1
        or.l    12(sp),d1
        add.w   d1,d1
        swap    d1
        ori.l   #0x40000003,d1
        move.l  d1,4(a1)
1:
        move.b  (a0)+,d0
        subi.b  #0x20,d0
        move.w  d0,(a1)
        tst.b   (a0)
        bne.b   1b
        rts

put_chr:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  16(sp),d1
        lsl.l   #6,d1
        or.l    12(sp),d1
        add.w   d1,d1
        swap    d1
        ori.l   #0x40000003,d1
        move.l  d1,4(a1)
        move.l  a0,d1
        move.b  d1,d0
        subi.b  #0x20,d0
        move.w  d0,(a1)
        rts

delay:
        move.l  4(sp),d0
        add.l   0xA1201C,d0
0:
        cmp.l   0xA1201C,d0
        bgt.b   0b
        rts

set_palette:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.l  12(sp),d1
        add.w   d0,d0
        swap    d0
        ori.l   #0xC0000000,d0
        subq.w  #1,d1
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  d0,4(a1)
0:
        move.w  (a0)+,(a1)
        dbra    d1,0b
        rts

z80_busrequest:
        move.l  4(sp),d0
        andi.w  #0x0100,d0
        move.w  d0,0xA11100
0:
        move.w  0xA11100,d1
        and.w   d0,d1
        bne.b   0b
        rts

z80_reset:
        move.l  4(sp),d0
        andi.w  #0x0100,d0
        move.w  d0,0xA11200
        rts

z80_memclr:
        movea.l 4(sp),a1
        move.l  8(sp),d0
        subq.w  #1,d0
        moveq   #0,d1
0:
        move.b  d1,(a1)+
        dbra    d0,0b
        rts

z80_memcpy:
        movea.l 4(sp),a1
        movea.l 8(sp),a0
        move.l  12(sp),d0
        subq.w  #1,d0
0:
        move.b  (a0)+,d1
        move.b  d1,(a1)+
        dbra    d0,0b
        rts

dma_screen:
        move.l  4(sp),d0
        move.l  8(sp),d1
        movem.l d2-d7/a2-a6,-(sp)
        move.w  #0x2700,sr
        move.l  #0x93E0948C,d2
        tst.b   d1
        beq.b   0f
        move.l  #0x934094AD,d2
0:
        move.l  #0x96009500,d3
        lsr.l   #1,d0
        move.b  d0,d3
        swap    d3
        lsr.l   #8,d0
        move.b  d0,d3
        move.l  #0x81149700,d4
        lsr.l   #8,d0
        andi.w  #0x007F,d0
        move.b  d0,d4
        swap    d4
        move.l  #0xC0000080,d5
        moveq   #0,d0
        lea     0xC00000,a2
        lea     0xC00004,a3
        moveq   #31,d1
        move.l  #0xC0000000,(a3)
1:
        move.l  d0,(a2)
        dbra    d1,1b
        move.w  #0x8F00,(a3)
        cmpi.l  #0x934094AD,d2
        beq.b   dma_wide
        move.w  #0x8C00,(a3)
        bra.w   dma_narrow

dma_wide:
        moveq   #0,d0
        move.w  #0x8154,(a3)
        move.l  #0x40000000,(a3)
1:
        btst    #3,1(a3)
        beq.b   1b
2:
        btst    #3,1(a3)
        bne.b   2b

        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.w  d0,(a2)
        nop
        nop
        nop
        nop
        nop
        nop
        move.l  d2,(a3)
        nop
        nop
        move.l  d3,(a3)
        nop
        nop
        move.l  d4,(a3)
        nop
        nop
        move.l  d5,(a3)
        btst    #7,0xA1200F
        beq.b   4f
        bset    #1,0xA12003
3:
        btst    #1,0xA12003
        bne.b   3b
4:
        pea     0.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA12018
        pea     1.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA1201A
        move.l  0xA1201C,d0
        addq.l  #1,d0
        move.l  d0,0xA1201C
        move.w  0xA12000,d0
        ori.w   #0x0100,d0
        move.w  d0,0xA12000
        moveq   #0x7F,d0
        and.b   0xA1200F,d0
        cmpi.b  #MD_CMD_DMA_SCREEN,d0
        bne.b   exit_wide
        bra.w   dma_wide
exit_wide:
        bsr.w   md_init_hw
        movem.l (sp)+,d2-d7/a2-a6
        rts

dma_narrow:
        moveq   #0,d0
        move.w  #0x8154,(a3)
        move.l  #0x40000000,(a3)
1:
        btst    #3,1(a3)
        beq.b   1b
2:
        btst    #3,1(a3)
        bne.b   2b

        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        nop
        nop
        nop
        nop
        nop
        nop
        move.l  d2,(a3)
        nop
        nop
        move.l  d3,(a3)
        nop
        nop
        move.l  d4,(a3)
        nop
        nop
        move.l  d5,(a3)
        btst    #7,0xA1200F
        beq.b   4f
        bset    #1,0xA12003
3:
        btst    #1,0xA12003
        bne.b   3b
4:
        pea     0.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA12018
        pea     1.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA1201A
        move.l  0xA1201C,d0
        addq.l  #1,d0
        move.l  d0,0xA1201C
        move.w  0xA12000,d0
        ori.w   #0x0100,d0
        move.w  d0,0xA12000
        moveq   #0x7F,d0
        and.b   0xA1200F,d0
        cmpi.b  #MD_CMD_DMA_SCREEN,d0
        bne.b   exit_narrow
        bra.w   dma_narrow
exit_narrow:
        bsr.w   md_init_hw
        movem.l (sp)+,d2-d7/a2-a6
        rts

init_32x:
        pea     19.l
        pea     12.l
        pea     0.l
        pea     message0(pc)
        bsr     put_str
        lea     16(sp),sp

        move.l  0xA130EC,d0
        cmpi.l  #0x4D415253,d0
        beq.b   0f
        moveq   #-2,d0
        rts
0:
        pea     20.l
        pea     12.l
        pea     0.l
        pea     message1(pc)
        bsr     put_str
        lea     16(sp),sp

        .if SCD_MODE1
| MODE1 (R6, as the CD32X BootCamp's listener): the cartridge boot already started the 32X - its boot
| ROMs loaded the loader from the cart and wrote M_OK / S_OK.  No SH2 reset here: after one the boot
| ROMs would look for the cartridge's start-up again.
        move.w  #0x2700,sr
        .else
        lea     0xA15000,a0
        move.l  #500000,d1
1:
        btst    #7,0x0101(a0)
        bne.b   2f
        subq.l  #1,d1
        bne.b   1b
        moveq   #-5,d0
        rts
2:
        move.w  #0x2700,sr
        move.b  #1,0x0101(a0)
        move.w  #19170,d1
1:
        dbra    d1,1b
        .endif

        pea     21.l
        pea     12.l
        pea     0.l
        pea     message2(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        moveq   #0,d0
        .if SCD_MODE1 == 0                  /* MODE1: M_OK / S_OK stay; the 32X is on already */
        move.l  d0,0x0120(a0)
        move.l  d0,0x0124(a0)
        move.b  #3,0x0101(a0)
        .endif
2:
        bclr    #7,0x0100(a0)
        bne.b   2b
        move.w  d0,0x0102(a0)
        move.w  d0,0x0104(a0)
        move.w  d0,0x0106(a0)
        move.l  d0,0x0108(a0)
        move.l  d0,0x010C(a0)
        move.w  d0,0x0110(a0)
        move.w  d0,0x0130(a0)
        move.w  d0,0x0132(a0)
        move.w  d0,0x0138(a0)
        move.w  d0,0x0180(a0)
        move.w  d0,0x0182(a0)
3:
        bclr    #0,0x018B(a0)
        bne.b   3b
        moveq   #-1,d1
        lea     0x840000,a1
4:
        move.w  d0,(a1)+
        dbra    d1,4b
5:
        bset    #0,0x018B(a0)
        beq.b   5b
        moveq   #-1,d1
        lea     0x840000,a1
6:
        move.w  d0,(a1)+
        dbra    d1,6b
11:
        bclr    #0,0x018B(a0)
        bne.b   11b

        moveq   #0x7F,d1
        lea     0xA15200,a1
7:
        move.l  d0,(a1)+
        dbra    d1,7b

        pea     22.l
        pea     12.l
        pea     0.l
        pea     message3(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        .if SCD_MODE1 == 0                  /* MODE1: no CD boot table, no "_CD_" */
        move.l  0x0120(a0),d0
        cmpi.l  #0x53444552,d0
        bne.b   8f
        move.w  #0x2000,sr
        moveq   #-3,d0
        rts
8:
        pea     23.l
        pea     12.l
        pea     0.l
        pea     message4(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        movea.l 4(sp),a0
        move.l  8(sp),d0
        lea     0x840000,a1
9:
        move.l  (a0)+,(a1)+
        subq.l  #4,d0
        bgt.b   9b

        pea     24.l
        pea     12.l
        pea     0.l
        pea     0x840000.l
        bsr     put_str
        lea     16(sp),sp

        lea     0xA15000,a0
        .endif
10:
        bset    #7,0x0100(a0)
        beq.b   10b

        .if SCD_MODE1 == 0
        move.l  #0x5F43445F,0x0120(a0)
        .endif

        pea     25.l
        pea     12.l
        pea     0.l
        pea     message5(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        clr.w   -(sp)
        move.l  #1000000,d1
0:
        move.l  0x0120(a0),d0
        cmpi.l  #0x4D5F4F4B,d0
        beq.b   1f
        subq.l  #1,d1
        bne.b   0b
        ori.w   #1,(sp)
1:
        move.l  #250000,d1
2:
        move.l  0x0124(a0),d0
        cmpi.l  #0x535F4F4B,d0
        beq.b   3f
        subq.l  #1,d1
        bne.b   2b
        ori.w   #2,(sp)
3:
        pea     26.l
        pea     12.l
        pea     0.l
        pea     message6(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        moveq   #0,d7
        moveq   #0,d0
        move.l  d0,0x0120(a0)
        move.l  d0,0x0124(a0)

        move.w  #0x2000,sr
        moveq   #0,d0
        move.w  (sp)+,d0
        rts
15:
        addq.l  #2,sp
        move.l  d0,0xA12018
        move.w  #0x2000,sr
        moveq   #-3,d0
        rts

read_32x_16:
        movea.l 4(sp),a0
        moveq   #0,d0
        move.w  (a0),d0
        rts

write_32x_16:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.w  d0,(a0)
        rts

read_32x_32:
        movea.l 4(sp),a0
        move.l  (a0),d0
        rts

write_32x_32:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.l  d0,(a0)
        rts

copy_to_cart:
        movea.l 4(sp),a0
        movea.l 8(sp),a1
        move.l  12(sp),d0
        subq.l  #1,d0
        bmi.b   2f
1:
        move.b  (a0)+,(a1)
        addq.l  #2,a1
        dbra    d0,1b
2:
        moveq   #0,d0
        rts

        .if SCD_MODE1
| ============================================================================================
| SONIC REV R6 - MODE1: the cartridge-boot routines (CD32X-BootCamp stack/subcpu/hw_md.s, where
| they are console-proven; the EverDrive facts are MegaEverDrivePro_16MB_RAM/docs/01).
| ============================================================================================
| MODE1-1: d1 = an argument of command d0, as the Sub-CPU gives it (CD-boot addresses) -> d1 for a
| cartridge boot.  Word RAM 0x200000-0x3FFFFF -> 0x600000-0x7FFFFF; the cart 0x400000-0x7FFFFF ->
| 0x000000-0x3FFFFF.  Commands 19-22 (32X register reads and writes) are left alone: their arguments
| are values - pads, lengths, chunk numbers - not addresses.
m1_arg:
        cmpi.b  #19,d0
        blo.b   1f
        cmpi.b  #22,d0
        bls.b   9f
1:
        cmpi.l  #0x200000,d1
        blo.b   9f
        cmpi.l  #0x800000,d1
        bhs.b   9f
        cmpi.l  #0x400000,d1
        bhs.b   2f
        addi.l  #0x400000,d1            /* Word RAM */
        rts
2:
        subi.l  #0x400000,d1            /* the cart */
9:
        rts

| MODE1-3: d0 bytes (a0) -> (a1), d3 = 0 bytes / 1 words.  One side may be the Sega CD (Word RAM at
| 0x600000-0x7FFFFF in this map): that side is read or written with RV = 0 through a 1 KB buffer in
| work RAM, because on the console a Word RAM read made with RV = 1 comes out garbled in cartridge
| mode.  The cart side is done with RV = 1 and interrupts off.
m1_xfer:
        movem.l d1-d4/a0-a4,-(sp)
        lea     m1_buf(pc),a4
1:
        tst.l   d0
        ble.b   9f
        move.l  d0,d2                   /* d2 = this block's length */
        cmpi.l  #1024,d2
        bls.b   2f
        move.l  #1024,d2
2:
        sub.l   d2,d0
        cmpa.l  #0x600000,a0
        blo.b   4f
        movea.l a1,a2                   /* the source is the Sega CD: to the buffer with RV = 0 */
        movea.l a4,a1
        bsr     m1_cpy
        movea.l a2,a1
        movea.l a0,a2
        movea.l a4,a0                   /* then the buffer to the cart side with RV = 1 */
        moveq   #0,d4
        bsr     m1_cart
        movea.l a2,a0
        bra.b   1b
4:
        cmpa.l  #0x600000,a1
        blo.b   6f
        movea.l a1,a2                   /* the destination is the Sega CD: the cart to the buffer */
        movea.l a4,a1                   /* with RV = 1 ... */
        moveq   #1,d4
        bsr     m1_cart
        movea.l a2,a1
        movea.l a0,a2
        movea.l a4,a0                   /* ... then the buffer there with RV = 0 */
        bsr     m1_cpy
        movea.l a2,a0
        bra.b   1b
6:
        moveq   #0,d4
        bsr     m1_cart                 /* neither side is the Sega CD: straight, RV = 1 */
        bra.b   1b
9:
        movem.l (sp)+,d1-d4/a0-a4
        moveq   #0,d0
        rts

| the cart side of one block: d2 bytes, d4 = 0 the cart is a1 (a write), 1 the cart is a0 (a read).
| Three cases: the mapper's own registers (0xA130xx: the write itself needs RV = 1, and a write to
| CTRL0 tells us which bank window 0 shows); the EverDrive's window 0, cart 0x000000-0x07FFFF, which
| the 68000 cannot write at all with the 32X on (console, docs/01 rule 1) and so goes through window 1
| with window 0's bank in it; and everything else, straight.
m1_cart:
        move.l  a1,d1
        andi.l  #0xFFFFFF00,d1
        cmpi.l  #0xA13000,d1
        bne.b   m1_c1
        tst.w   d4                      /* a write to a mapper register */
        bne.b   m1_c1
        move.l  a1,d1
        cmpi.l  #0xA130F0,d1            /* CTRL0: its low 5 bits are window 0's bank - remember it */
        bne     m1_regcpy
        move.w  (a0),d1
        andi.w  #31,d1
        lea     m1_w0bank(pc),a3        /* the listener runs at 0xFF1000: PC-relative, not absolute */
        move.w  d1,(a3)
        bra     m1_regcpy               /* a mapper register, not memory: RV = 1, and W stays off */
m1_c1:
        tst.w   d4
        bne.b   1f
        move.l  a1,d1                   /* a write: is the destination in window 0? */
        bra.b   2f
1:      move.l  a0,d1                   /* a read: is the source in window 0? */
2:      cmpi.l  #0x080000,d1
        bhs.b   m1_rvcpy
        move.w  sr,-(sp)                /* MODE1-3: window 0 through window 1 */
        move.w  #0x2700,sr
        bset    #0,0xA15107
        move.w  m1_w0bank(pc),d1        /* window 1 = the bank window 0 shows */
        move.w  d1,0xA130F2
        tst.w   d4
        bne.b   3f
        adda.l  #0x080000,a1
        bsr.b   m1_cpy_w                /* a write into the cart: W on inside */
        suba.l  #0x080000,a1
        bra.b   4f
3:      adda.l  #0x080000,a0
        bsr.b   m1_cpy                  /* a read of the cart: W stays off */
        suba.l  #0x080000,a0
4:      move.w  m1_w0bank(pc),d1        /* window 1 = its own bank again (window 0's page, second half) */
        addq.w  #1,d1
        move.w  d1,0xA130F2
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        rts

| d2 bytes with RV = 1 and no interrupt; d4 = 0 (a write into the cart) turns W on for the copy.
m1_rvcpy:
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        tst.w   d4
        bne.b   1f
        bsr.b   m1_cpy_w
        bra.b   2f
1:      bsr.b   m1_cpy
2:      bclr    #0,0xA15107
        move.w  (sp)+,sr
        rts

| MODE1-6: the copy with the EverDrive's W on, and nothing else written by the 68000 in between
| (the call's push is before it, the return is a read).  RV is already 1.
m1_cpy_w:
        move.w  m1_w0bank(pc),d1
        ori.w   #SCD_M1CTRL0_W,d1
        move.w  d1,0xA130F0             /* W on, window 0 left where the game put it */
        bsr.b   m1_cpy
        move.w  m1_w0bank(pc),d1
        ori.w   #SCD_M1CTRL0,d1
        move.w  d1,0xA130F0             /* W off again, the same bank */
        rts
| a write to the mapper's own registers: RV = 1 and no interrupt, no W - W guards the memory, not
| these.  RV must be set HERE: the buffered path arrives with RV = 0, and a mapper write with RV = 0
| never reaches the EverDrive (console, 25 Oct 2026, docs/12 of the CD32X BootCamp) - the page would
| silently never switch.
m1_regcpy:
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        bsr.b   m1_cpy
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        rts

| d2 bytes (a0)+ -> (a1)+, in bytes (d3 = 0) or words (d3 = 1, d2 rounded up to a word)
m1_cpy:
        move.l  d2,d1
        tst.w   d3
        bne.b   2f
        subq.l  #1,d1
        bmi.b   9f
1:
        move.b  (a0)+,(a1)+
        dbra    d1,1b
        rts
2:
        addq.l  #1,d1
        lsr.l   #1,d1
        subq.l  #1,d1
        bmi.b   9f
3:
        move.w  (a0)+,(a1)+
        dbra    d1,3b
9:
        rts

        .align  2
m1_w0bank:
        .word   0                       /* the bank window 0 shows (EDCD hands over with banks 0-7 linear) */
m1_buf:
        .space  1024
        .endif

safe_cart_xfer:
        .if SCD_MODE1
        movea.l 4(sp),a0                /* MODE1-3: src, dst, length - through m1_xfer (bytes) */
        movea.l 8(sp),a1
        move.l  12(sp),d0
        moveq   #0,d3
        bra     m1_xfer
        .endif
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3

        move.w  sr,-(sp)
        move.w  #0x2700,sr

        bset    #0,0xA15107

        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        beq.b   3f
2:
        move.b  (a0)+,(a1)+
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107

        move.w  (sp)+,sr

        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

safe_cart_xfer_w:
        .if SCD_MODE1
        movea.l 4(sp),a0                /* MODE1-3: src, dst, length - through m1_xfer (words) */
        movea.l 8(sp),a1
        move.l  12(sp),d0
        moveq   #1,d3
        bra     m1_xfer
        .endif
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3

        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107

        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        ble.b   3f
2:
        move.w  (a0)+,(a1)+
        subq.l  #2,d0
        bgt.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

bram_put:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        ble.b   3f
2:
        move.b  (a0)+,(a1)
        addq.l  #2,a1
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

bram_get:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        ble.b   3f
2:
        move.b  (a0),(a1)+
        addq.l  #2,a0
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

bram_to_fb:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        lsr.l   #1,d0
        beq.b   3f
        moveq   #0,d1
2:
        move.b  (a0),d1
        addq.l  #2,a0
        lsl.w   #8,d1
        move.b  (a0),d1
        addq.l  #2,a0
        ori.w   #0x8000,d1
        move.w  d1,(a1)+
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

copy_from_cart:
        movea.l 4(sp),a0
        movea.l 8(sp),a1
        move.l  12(sp),d0
        lsr.l   #1,d0
        beq.b   2f
        moveq   #0,d1
1:
        move.b  (a0),d1
        addq.l  #2,a0
        lsl.w   #8,d1
        move.b  (a0),d1
        addq.l  #2,a0
        ori.w   #0x8000,d1
        move.w  d1,(a1)+
        subq.l  #1,d0
        bne.b   1b
2:
        moveq   #0,d0
        rts

copy_words:
        movea.l 4(sp),a0
        movea.l 8(sp),a1
        move.l  12(sp),d0
        lsr.l   #1,d0
        beq.b   2f
        subq.l  #1,d0
        move.l  16(sp),d1
        tst.l   d1
        bne.b   3f
1:
        move.w  (a0)+,(a1)+
        dbra    d0,1b
2:
        moveq   #0,d0
        rts
3:
        move.w  (a0)+,d1
        or.w    #0x8000,d1
        move.w  d1,(a1)+
        dbra    d0,3b
        moveq   #0,d0
        rts

message0:
        .asciz  "Init 32X V2.9"
message1:
        .asciz  "Found 32X"
message2:
        .asciz  "Activated 32X"
message3:
        .asciz  "Cleared 32X"
message4:
        .asciz  "32X Waiting"
message5:
        .asciz  "32X Handshake"
message6:
        .asciz  "32X GO!"

        .align  4

vblank_int:
        movem.l d0-d1/a0-a1,-(sp)
        tst.w   SGB_FLAG                /* SONIC REV R1: the planes first, early in vertical blank */
        beq.b   vbl_dry
        bsr.w   sgb_water               /* SONIC REV R4D: the water line first (the normal colours back) */
        bsr.w   sgb_vblank
        bra.b   vbl_pads
vbl_dry:
        clr.w   SGB_HON                 /* SONIC REV R4D: the planes off - no water line (no VDP: the */
        clr.w   SGB_WON                 /* command loop may be using it; the listener's VDP set-up    */
        clr.w   SGB_WLINE               /* turns the H-int off)                                        */
vbl_pads:

        pea     0.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA12018

        pea     1.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA1201A

        move.l  0xA1201C,d0
        addq.l  #1,d0
        move.l  d0,0xA1201C
        btst    #0,0xA15101             /* HW7: only once the 32X is on (ADEN bit) */
        beq.b   1f
        tst.w   SGB_FLAG                /* SONIC REV R1: COMM6 answers the plane records instead */
        bne.b   1f
        move.w  d0,0xA15126             /* HW7: 68K heartbeat -> 32X COMM6 (SH2 reads it 1x/s) */
1:

        move.w  0xA12000,d0
        ori.w   #0x0100,d0
        move.w  d0,0xA12000

        movem.l (sp)+,d0-d1/a0-a1
        rte

chilly_memcpy:
        movea.l 4(sp),a1
        movea.l 8(sp),a0
        move.l  12(sp),d0
0:
        move.b  (a0)+,d1
        move.b  d1,(a1)+
        subi.l  #1,d0
        bgt.b   0b
        moveq.l #0,d0
        rts

| SONIC REV R4K: chilly_rv_memcpy copies 4 bytes a step when the source, the destination and the length are all
| multiples of 4 (every load into the cart: 16 KB pieces from Word RAM 0x200000). The byte loop cost about 42 clocks
| a byte - 0.7 s of the 2.0 s each 128 KB of a load took (measured in ares, 20 Oct); move.l is two word writes to the
| 16-bit cart, the same bytes in the same places. Anything else (the 2-byte cart probe) takes the byte loop, as before.
| Interrupts stay on and RV = 1 as before.
chilly_rv_memcpy:
        .if SCD_MODE1
        movea.l 4(sp),a1                /* MODE1-3: dst, src, length - through m1_xfer (bytes) */
        movea.l 8(sp),a0
        move.l  12(sp),d0
        moveq   #0,d3
        bra     m1_xfer
        .endif
        bset    #0,0xA15107
        movea.l 4(sp),a1
        movea.l 8(sp),a0
        move.l  12(sp),d0
        move.w  a0,d1
        or.w    d0,d1
        move.l  d2,-(sp)
        move.w  a1,d2
        or.w    d2,d1
        move.l  (sp)+,d2
        andi.w  #3,d1
        bne.b   0f
        lsr.l   #2,d0
        beq.b   2f
1:
        move.l  (a0)+,(a1)+
        subq.l  #1,d0
        bne.b   1b
        bra.b   2f
0:
        move.b  (a0)+,d1
        move.b  d1,(a1)+
        subi.l  #1,d0
        bgt.b   0b
2:
        bclr    #0,0xA15107
        moveq.l #0,d0
        rts

| =============================================================================
| 68K EXCEPTION TRAP (V71, restored in K29) - DIAGNOSTIC, changes nothing unless the 68K
| faults.  Hooks vectors 2..11 (bus, address, illegal, div0, CHK, TRAPV, privilege, trace,
| line-A, line-F): where a vector points into RAM (the BIOS jump table), "jmp <our stub>" is
| written there; vectors pointing to ROM are left alone.  Returns how many were hooked.
| On a fault: row 21 "68K EXC Vnn  FRAME:", row 22 = first 7 words of the exception frame
|   bus/address error (V02/V03): FC  ADDR-HI ADDR-LO  IR  SR  PC-HI PC-LO
|   all others:                  SR  PC-HI PC-LO  (rest = stack contents)
| then the 68K stops (the message stays on screen).  Needs the Genesis display ON to be seen.
| =============================================================================
install_exc:
        movem.l d2/a2-a3,-(sp)
        lea     exc_stubs(pc),a3
        moveq   #0,d1                   /* hooked count */
        moveq   #2,d2                   /* vector number 2..11 */
1:
        move.l  d2,d0
        lsl.l   #2,d0
        movea.l d0,a0
        movea.l (a0),a2                 /* where this vector goes */
        cmpa.l  #0xFF0000,a2
        blo.b   2f                      /* not RAM - leave it */
        move.w  #0x4EF9,(a2)            /* jmp abs.l */
        move.l  a3,2(a2)                /*     -> our stub */
        addq.l  #1,d1
2:
        addq.l  #8,a3                   /* next stub (8 bytes each) */
        addq.l  #1,d2
        cmpi.l  #12,d2
        bne.b   1b
        move.l  d1,d0
        movem.l (sp)+,d2/a2-a3
        rts

        .align  2
exc_stubs:                              /* each: move.w #vec,-(sp) / bra.w exc_common = 8 bytes */
        move.w  #2,-(sp)
        bra.w   exc_common
        move.w  #3,-(sp)
        bra.w   exc_common
        move.w  #4,-(sp)
        bra.w   exc_common
        move.w  #5,-(sp)
        bra.w   exc_common
        move.w  #6,-(sp)
        bra.w   exc_common
        move.w  #7,-(sp)
        bra.w   exc_common
        move.w  #8,-(sp)
        bra.w   exc_common
        move.w  #9,-(sp)
        bra.w   exc_common
        move.w  #10,-(sp)
        bra.w   exc_common
        move.w  #11,-(sp)
        bra.w   exc_common

exc_common:
        move.w  #0x2700,sr
        move.w  (sp)+,d6                /* vector number */
        movea.l sp,a5                   /* exception frame */
        clr.w   SGB_FLAG                /* SONIC REV R1F: the planes' cells replaced the font - the  */
        bsr.w   md_init_hw              /* message came out in level tiles (R1E). Records off, the   */
        move.w  #0x2700,sr              /* listener's VDP set-up and font back (keeps d2-d7/a2-a5),  */
                                        /* interrupts off again                                      */
        move.w  #0x8174,0xC00004        /* K29: Genesis display ON so the message shows */
        lea     exc_buf(pc),a0
        lea     exc_txt1(pc),a1
3:      move.b  (a1)+,(a0)+             /* "68K EXC V" */
        bne.b   3b
        subq.l  #1,a0
        move.l  d6,d0
        moveq   #1,d1                   /* 2 hex digits */
        bsr     exc_hex
        lea     exc_txt2(pc),a1
4:      move.b  (a1)+,(a0)+
        bne.b   4b
        pea     21.l
        pea     2.l
        pea     0x4000.l                /* red */
        pea     exc_buf(pc)
        bsr     put_str
        lea     16(sp),sp

        lea     exc_buf(pc),a0
        moveq   #6,d5                   /* 7 words */
5:      move.w  (a5)+,d0
        moveq   #3,d1                   /* 4 hex digits */
        bsr     exc_hex
        move.b  #' ',(a0)+
        dbra    d5,5b
        clr.b   (a0)
        pea     22.l
        pea     2.l
        pea     0x4000.l
        pea     exc_buf(pc)
        bsr     put_str
        lea     16(sp),sp
6:      bra.b   6b                      /* stop here - leave the message on screen */

| d0 = value, d1 = digits-1 (low digits of d0), a0 = output -> advanced
exc_hex:
        move.l  d1,d3
        addq.l  #1,d3
        lsl.l   #2,d3                   /* bits to print */
        ror.l   d3,d0                   /* bring them to the top */
7:      rol.l   #4,d0
        move.b  d0,d4
        andi.b  #15,d4
        addi.b  #'0',d4
        cmpi.b  #'9',d4
        bls.b   8f
        addq.b  #7,d4
8:      move.b  d4,(a0)+
        dbra    d1,7b
        rts

exc_txt1:
        .asciz  "68K EXC V"
exc_txt2:
        .asciz  "  FRAME:                    "
        .align  2
exc_buf:
        .space  48

| =============================================================================
| SONIC 2011 REV R1 - the Genesis planes (md/sgb.h)
| sgb_poll (command loop): the SH2 has handed the 32X frame buffer over (Kobo give_fb: only the
| SH2 switches FM) and put a new record id into COMM4. Copy the record (frame buffer, Genesis side
| 0x840000) into work RAM and answer with the same id in COMM6 - the SH2 then takes the frame
| buffer back. Only when the last record has gone to the VDP (SGB_READY 0).
| Keeps every register (R1G).
| =============================================================================
sgb_poll:
        movem.l d0-d1/a0-a1,-(sp)       /* R1G: d0 kept too - md_check_cmd loads only its low byte
                                           (move.b), so a record id left in bits 8-15 (ids from 256 on)
                                           sent the dispatch to cmd_table itself: R1F's 68K EXC V0B at
                                           PC 00FF10EA (= cmd_table), return address 00FF10DC */
        tst.w   SGB_READY
        bne.b   9f
        move.w  0xA15124,d0             /* COMM4: record id (never 0) */
        beq.b   9f
        cmp.w   SGB_LAST,d0
        beq.b   9f
        cmp.w   0xA15124,d0             /* HW10: the same twice - not a torn read */
        bne.b   9f
        lea     0x840000,a0             /* the record area at the start of the frame buffer */
        move.w  (a0)+,d1                /* length: words after this one */
        cmpi.w  #SGB_RECMAX,d1
        bhi.b   6f                      /* not a record: answer, show nothing */
        lea     SGB_REC,a1
        move.w  d1,(a1)+
        subq.w  #1,d1
        bmi.b   6f
7:      move.w  (a0)+,(a1)+
        dbra    d1,7b
        move.w  #1,SGB_READY
        bra.b   8f
6:      move.w  #1,SGB_BAD              /* R1G: the backdrop turns red in vertical blank (sgb_vblank) */
8:      move.w  d0,SGB_LAST
        move.w  d0,0xA15126             /* COMM6: record taken */
9:      movem.l (sp)+,d0-d1/a0-a1
        rts

| sgb_vblank (vertical blank, interrupt level 6): the waiting record into the VDP - name-table
| groups, vertical scroll, per-line horizontal scroll (table at 0xFC00, plane A then plane B with
| auto-increment 4), palette changes. Reads only work RAM. Saves what it uses (the handler saves
| d0-d1 / a0-a1 itself).
sgb_vblank:
        tst.w   SGB_BAD                 /* R1G: a record that was not one - backdrop (CRAM 0) red */
        beq.b   98f
        move.l  #0xC0000000,0xC00004
        move.w  #0x000E,0xC00000
98:     tst.w   SGB_READY
        beq.w   99f
        movem.l d2-d4/a2-a3,-(sp)
        lea     SGB_REC+2,a0            /* after the length word */
        lea     0xC00004,a2             /* VDP control */
        lea     0xC00000,a3             /* VDP data */
        move.w  #0x8F02,(a2)            /* auto-increment 2 */
        move.w  (a0)+,d4                /* name-table groups */
        bra.b   3f
1:      move.w  (a0)+,d0                /* VRAM address -> write command */
        moveq   #0,d1
        move.w  d0,d1
        andi.w  #0x3FFF,d1
        ori.w   #0x4000,d1
        swap    d1
        rol.w   #2,d0
        andi.w  #3,d0
        move.w  d0,d1
        move.l  d1,(a2)
        move.w  (a0)+,d1                /* words in the group */
        subq.w  #1,d1
        bmi.b   3f
2:      move.w  (a0)+,(a3)
        dbra    d1,2b
3:      dbra    d4,1b
        move.l  #0x40000010,(a2)        /* VSRAM 0: plane A, plane B */
        move.w  (a0)+,(a3)
        move.w  (a0)+,(a3)
        move.w  #0x8F04,(a2)            /* h-scroll table: every 4 bytes */
        move.l  #0x7C000003,(a2)        /* VRAM 0xFC00: plane A of each line */
        bsr.w   sgb_runs
        move.l  #0x7C020003,(a2)        /* VRAM 0xFC02: plane B of each line */
        bsr.w   sgb_runs
        move.w  #0x8F02,(a2)
        move.w  (a0)+,d4                /* palette changes */
        lea     SGB_NCRAM,a1            /* SONIC REV R4D: and into the normal colours' shadow */
        bra.b   5f
4:      move.w  (a0)+,d0                /* CRAM index 0-63 */
        add.w   d0,d0
        move.w  d0,d2
        andi.w  #0x7E,d2
        swap    d0
        clr.w   d0
        ori.l   #0xC0000000,d0
        move.l  d0,(a2)
        move.w  (a0)+,d0
        move.w  d0,(a3)
        move.w  d0,0(a1,d2.w)
5:      dbra    d4,4b
        clr.w   SGB_READY
        movem.l (sp)+,d2-d4/a2-a3
99:     rts

| SONIC REV R4D: sgb_water (vertical blank, before sgb_vblank; SGB_FLAG is 1). The waiting record's water trailer (md/sgb.h,
| from the record's end): the water line and the water colours' changes - a record without one: no water line. CRAM
| holding the water colours (the last frame's H-int): the normal ones back (the shadow; sgb_vblank's palette changes
| come after). Then this frame's H-int: register 10 = the water line - 1 (the H-int at that line's end), IE1 on; no water
| line: IE1 off, register 10 = 255 (no H-int counted out while it is off: one left over would come the moment IE1 went on).
| Uses d0-d1 / a0-a1 (saved by vblank_int).
sgb_water:
        tst.w   SGB_READY
        beq.b   2f
        lea     SGB_REC,a0
        move.w  (a0),d0                 /* length: words after the length word */
        cmpi.w  #SGB_RECMAX,d0
        bhi.b   1f
        cmpi.w  #3,d0
        blo.b   1f
        add.w   d0,d0
        lea     0(a0,d0.w),a0           /* the record's last word */
        cmpi.w  #SGB_WTAG,(a0)
        bne.b   1f
        move.w  -(a0),SGB_WLINE         /* W */
        move.w  -(a0),d0                /* Q */
        cmpi.w  #64,d0
        bhi.b   2f
        move.w  d0,d1
        add.w   d1,d1
        add.w   d1,d1
        suba.w  d1,a0                   /* the first of the Q changes */
        lea     SGB_WCRAM,a1
        bra.b   11f
10:     move.w  (a0)+,d1                /* CRAM index */
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a0)+,0(a1,d1.w)        /* its water colour */
11:     dbra    d0,10b
        bra.b   2f
1:      clr.w   SGB_WLINE               /* no trailer: no water line */
2:      lea     0xC00004,a1             /* VDP control */
        tst.w   SGB_WON
        beq.w   3f
        move.w  #0x8F02,(a1)
        move.l  #0xC0000000,(a1)        /* CRAM 0 */
        lea     SGB_NCRAM,a0
        .rept   32
        move.l  (a0)+,-4(a1)            /* the normal colours back */
        .endr
        clr.w   SGB_WON
3:      move.w  SGB_WLINE,d0
        beq.b   4f
        cmpi.w  #224,d0
        bhs.b   4f
        subq.w  #1,d0
        ori.w   #0x8A00,d0
        move.w  d0,(a1)                 /* register 10: the H-int at the end of line W - 1 */
        move.w  #0x8014,(a1)            /* register 0: IE1 on (the listener's 0x04 + bit 4) */
        move.w  #1,SGB_HON
        rts
4:      tst.w   SGB_HON
        beq.b   5f
        move.w  #0x8004,(a1)            /* IE1 off */
        move.w  #0x8AFF,(a1)
        clr.w   SGB_HON
5:      rts

| SONIC REV R4D: the H-int (level 4, through 0xFFFD0C): at the water line's start, the 64 water colours into CRAM - once a
| frame (SGB_WON), and only at or below the line (the VDP's V counter): one left over from before the vertical blank's set-up
| comes at once (in vertical blank, or above the line) and does nothing. Not wanted (SGB_HON 0, IE1 left on by an act
| before): IE1 off. Planes off (SGB_FLAG 0): nothing - the command loop may be using the VDP.
hint_int:
        move.w  #0x2700,sr
        tst.w   SGB_FLAG
        beq.w   9f
        tst.w   SGB_HON
        bne.b   1f
        move.w  #0x8004,0xC00004        /* IE1 off */
        move.w  #0x8AFF,0xC00004
        rte
1:      tst.w   SGB_WON
        bne.w   9f                      /* this frame's water colours are in */
        movem.l d0/a0-a1,-(sp)
        moveq   #0,d0
        move.b  0xC00008,d0             /* V counter */
        cmpi.w  #0xE0,d0
        bhs.w   8f                      /* vertical blank: left over */
        addq.w  #1,d0
        cmp.w   SGB_WLINE,d0
        blo.w   8f                      /* above line W - 1: left over */
        lea     0xC00004,a1
        move.w  #0x8F02,(a1)
        move.l  #0xC0000000,(a1)        /* CRAM 0 */
        lea     SGB_WCRAM,a0
        .rept   32
        move.l  (a0)+,-4(a1)
        .endr
        move.w  #1,SGB_WON
8:      movem.l (sp)+,d0/a0-a1
9:      rte

| a0 -> runs (count, count x {lines, value}): each value lines times into the VDP data port (a3)
sgb_runs:
        move.w  (a0)+,d4
        bra.b   2f
1:      move.w  (a0)+,d1                /* lines */
        move.w  (a0)+,d0                /* value */
        subq.w  #1,d1
        bmi.b   2f
3:      move.w  d0,(a3)
        dbra    d1,3b
2:      dbra    d4,1b
        rts

        .align  4
md_init_end:
