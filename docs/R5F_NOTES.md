# R5F – the title on the Genesis and the Sega CD's graphics chip (with R5D and R5E)

The owner's requests (21 Oct):
- "The clouds are supposed to be another scaling affect like the backgrounds on the special levels."
- "The loading is actually fine. No need to change any of that. No changes to the boot stack needed."
- "Focus on the clouds and the speed of the special levels and background effects."
- "The water and sky should be free. Thats how SEGA does it on the real segaCD."

R5F includes R5D and R5E, which were never delivered on their own. All three are built on R5C.

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

## R5E and R5F: the title

On the title, the 32X now draws only Sonic, the emblem and the text. Everything behind them is the Genesis's and the Sega
CD's:

| Part | Where | How it moves |
|---|---|---|
| **Clouds** | the graphics chip → window plane, lines 0-103, colours line 0 | the engine's cloud ceiling in perspective, each line its own start, step and depth |
| **Sky** | the 32X's colour 0 itself | — |
| **Mountains** | plane B, lines 100-167, colours line 1 | — |
| **Water** | plane B, lines 168-223, colours line 1 | 14 strips of 4 lines, each its own horizontal scroll, as the engine moves them |
| **Planet** | 8 hardware sprites behind the clouds, colours line 2 | bobs up and down as the engine moves it |

**`tools/build_floor.py` makes `FLOOR9.BIN`:**
- the clouds as the chip's stamps and map, and each screen line's start, step and depth from the engine's own matrix
  values;
- the mountains and the water as plane B's cells (124 cells, 12 colours) and its name table;
- the planet's cells in the order its 8 sprites use them.

**The clouds keep scrolling between the SH2's pictures.** For each picture the SH2 sends the engine's scroll value; the
Sub-CPU then adds 8 a frame, as the clouds object does. The chip starts the next picture as soon as the copy of the last
one ends. The switch to the new buffer happens only in the vertical blank or the first lines of the screen (the VDP's
line counter); otherwise it waits for the next vertical blank, so the clouds are never torn.

**The sky:** the 32X shows its colour 0 wherever the Genesis shows its backdrop. So colour 0 on the title is the sky's
colour, with no see-through bit; it shows in the gaps between the clouds. The mountains' own sky pixels (palette 128,
the same colour) are see-through for the same reason.

**For each picture the SH2 sends one record to the 68000**, by the same path the acts' planes use (`md/sgb.h`). It holds:
- plane B's scroll for each line (the water);
- the planet sprites' heights, when they moved;
- the colours that changed (CRAM 0-47, with the fades).

The 68000 writes the record to the VDP in the vertical blank (`sgb_poll` / `sgb_vblank` in the listener, unchanged). The
record goes where the frame buffer's line table is, and the SH2 writes the line table back afterwards.

**On the SH2:** each picture starts by clearing the frame buffer to colour 0. It no longer draws the sky, the clouds, the
planet, the mountains or the water.

## The copy into video RAM (the title and the special stages' floor)

- It is the VDP's DMA from Word RAM, not the 68000 copying word by word.
- A DMA from Word RAM gets its first word wrong. The 68000 never writes that word itself, because the records' vertical
  blank handler could come between a VDP address and its data.
- Instead, the pieces go from the buffer's end to its start. Each piece starts one word early, so its wrong first word
  lands on a word the next piece writes again.
- The last piece's wrong word lands in a spare cell before each buffer.

**The backup-RAM file no longer ends the floor.** Commands 56 and 59 use the BIOS's own buffers, not Word RAM. The title
loads that file just after the clouds begin, which used to end them.

## The boot stack

- `cd32x/sub_main.c`, command 80:
  - stage 9 is the title;
  - the floor's copy is a DMA;
  - while the title is on, the Sub-CPU turns the listener's record handling on (`SGB_FLAG`), as command 68 does for an
    act.
- **The 68000 listener (`kobo/hw_md.s`) is unchanged.** The DMA uses its existing commands 22 (a long write to the VDP's
  control port) and 34 (a VDP register). The records use its existing `sgb_poll` / `sgb_vblank`.
- `kobo/sub_stubs.c`, `kobo/scd.c` and `sh2_engboot.c` (`SH2-VR4I`) are unchanged.
