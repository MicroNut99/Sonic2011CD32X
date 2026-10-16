# Kobo CD32X - command and code ledger

Companion to `KOBO_CD32X_BOOT_PROCESS.md` (from Micronut99's ledger PDF, checked against `subcpu/hw_md.s`).

**Correction to the PDF:** error −5 is a timeout while waiting for bit 7 of `0xA15101` to become **set**
(adapter ready). `init_32x` loops on `btst #7,0x0101(a0)` / `bne` for 500,000 tries. The PDF said "to clear".

## 68000 commands (MD_CMD, sent by the Sub-CPU)
1 INIT_HW · 2 SET_SR (**disabled**, torn-read hazard) · 3 GET_PAD · 4 CLEAR_B · 5 SET_VRAM · 6 NEXT_VRAM ·
7 COPY_VRAM · 8 CLEAR_A · 9 PUT_STR · 10 PUT_CHR · 11 DELAY · 12 SET_PALETTE · 13 Z80_BUSREQUEST ·
14 Z80_RESET · 15 Z80_MEMCLR · 16 Z80_MEMCPY · 17 DMA_SCREEN · 18 INIT_32X · 19 READ_32X_16 ·
20 WRITE_32X_16 · 21 READ_32X_32 · 22 WRITE_32X_32 · 23 COPY_TO_CART · 24 COPY_FROM_CART ·
25 COPY_WORDS · 26 SAFE_CART_XFER · 27 SAFE_CART_XFER_W · 28 BRAM_PUT · 29 BRAM_TO_FB · 30 BRAM_GET ·
31 INSTALL_EXC · 32 CHILLY_MEMCPY · 33 CHILLY_RV_MEMCPY · 34 SET_VDP_REG · 35 END

## SH2 status codes (COMM12)
4B01 loading · 4B02 pack in cart · 4B03 gfx cart→SDRAM · 4B04 PCM pack · 4B05 backup RAM ·
4B51–4B54 palette/level/256-colour/swaps · 4B6n/4B7n tile set n · 4B06/4B07 tiles done, panel ·
4B08 music started (no CD data reads after this) · 4B10 running ·
4BE1 KOBOGFX.BIN not found · 4BE2 cart verify failed · 4BE3 no 'KGFX' magic · 4BE5 too many banks ·
4BE6 program past 0x0600C000

## Genesis init_32x errors
−2 no "MARS" at 0xA130EC · −3 32X boot ROM self-test "SDER" · −5 adapter-ready timeout (see correction)

## Diagnostic strip
Generic (keep for Sonic CD): **P** Sub heartbeat (top digit = CD drive status 0xFF8038: 0 stopped,
1 playing, 2 seeking, 3 scanning, 4 paused, 5 tray open, 9 reading TOC, B no disc/error; rest = passes/64),
**V** Genesis vblanks. Kobo-specific (to be redefined): T, S, 1/2/3, N, X/Y/D, B, E, L, G.
