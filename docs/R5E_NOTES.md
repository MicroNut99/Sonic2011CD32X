# R5E – the title's clouds on the Sega CD's graphics chip (and R5D)

The owner's requests (21 Oct): "The clouds are supposed to be another scaling affect like the backgrounds on the special
levels." – "The loading is actually fine. No need to change any of that. No changes to the boot stack needed." –
"Focus on the clouds and the speed of the special levels and background effects."

R5E includes R5D, which was never delivered. Both are built on R5C.

## R5D (in this build)

- **Power-on:** the title stays in page 0 of the cart, so the first title screen no longer loads at every power-on.
  R5C had put the next special stage in page 0, and that evicted the title. The giant ring's special stage now loads
  from the disc with the loading picture, as before R5C.
- **The first-time notice:** while the cart is filled for the first time, the loading picture reads **FIRST TIME
  SETTING UP THE CART** above the bar and **THIS TAKES A FEW MINUTES** below it.
- **The special stages' floor colours:** the Genesis floor takes its colours from the palette the engine uses on the
  floor's own lines.
- **Special stage speed-ups on the SH2 side, with the same pixels:**
  - the sky texture is copied a run at a time;
  - tile rows with no pixel in the sky layers are skipped.

## R5E: the title's clouds

The 2011 game draws the title's cloud ceiling as a 3D scene: Clouds.gif on a 16 × 10 grid, in perspective, scrolling
toward you. R5C's SH2 drew it, and that was the slowest part of the title's picture. R5E draws it the way the special
stages' floor is drawn.

**`tools/build_floor.py` makes `FLOOR9.BIN`:**
- the clouds as the chip's stamps and map;
- each screen line's start, step and depth, from the engine's own matrix values;
- the planet's cells.

**At the title, the Sub-CPU (SH2 command 80, stage 9):**
- The chip draws lines 0-111. The window plane shows them, rows 0-13, colours line 0.
- The planet is on plane B, colours line 1.
- The sky is the 32X's colour 0 itself.
- **Between pictures, the clouds keep scrolling by themselves.** For each picture the SH2 sends the engine's own scroll
  value. Between pictures the Sub-CPU adds 8 a frame, as the clouds object does.

**On the SH2:**
- It no longer draws the clouds, the planet or the sky rectangle. It clears those 112 lines to colour 0.
- Colour 0 has no see-through bit there. The 32X shows its colour 0 wherever the Genesis shows its backdrop, which is
  the gaps between the clouds. The Genesis shows wherever it has a pixel.

**Also:**
- **The copy into video RAM** is now the VDP's DMA instead of the 68000 copying word by word. A DMA from Word RAM reads
  a word late, so the source starts one word on and the first word is written by hand. This applies to the special
  stages' floor too.
- **The title's next picture starts as soon as the copy ends.** The switch to the new buffer waits for the vertical
  blank, while the chip is already drawing the next one.
- **The backup-RAM file no longer ends the floor.** Commands 56 and 59 use the BIOS's own buffers, not Word RAM. The
  title loads that file just after the clouds begin. In the first R5E builds, that ended the clouds and the SH2 drew
  them again.

## The boot stack

- `cd32x/sub_main.c`: command 80 has a stage 9 (the title), and the floor's copy is a DMA.
- **The 68000 listener (`kobo/hw_md.s`) is unchanged.** The DMA uses its existing commands 22 (a long write to the
  VDP's control port) and 7 (the first word).
