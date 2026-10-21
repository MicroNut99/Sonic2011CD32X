# R5C – the special stages' floor on the Sega CD's graphics chip, the next special stage in the cart, no BONUS

The owner's requests (20 Oct): "The Special stages have really low fps." – "SEGA did it using the Genesis and the
SegaCD. Its looks fantanstic. Use the same methods."

Later the same night: "If possible load all level special and bonus rounds. Then each level after that let the user know
that the next level needs to load everything for the next level to the cart." – "16MB CArt. USe it All" – (asked which
way) "Next special ready", the notice on the loading screen – "Remove the Bonus from the menu. There is nothing in it."

R5C is R5A / R5B (never delivered) and these, in one build.

The 1993 game draws the special stage floor with the Sega CD's stamp / rotation chip and shows it on the Genesis. R5
does the same. R4N's SH2s computed the floor for every picture themselves, and that was about 40 % of their time there.

## How it works

- **The floor files:** `tools/build_floor.py` makes `FLOOR1.BIN` – `FLOOR8.BIN`, one for each special stage, from the
  game's data. Each file holds the stage's floor as the chip's stamps (one 16 × 16 stamp per tile) and its stamp map
  (256 × 256 stamps = the 4096 × 4096 floor). The file is the chip's Word RAM image, 256 KB.
- **Colours:** the chip draws 4 bits a pixel, so each floor gets 15 colours. SS1 and SS4 have 16, and their two
  closest colours are merged.
- **When a special stage starts** (SH2 command 80 op 0): the Sub-CPU switches Word RAM to 2 Mbit mode, reads the file
  into it and sets the Genesis up. The floor goes on the window plane, lines 120-223, palette line 3.
- **For each picture the SH2 draws:**
  - The SH2 sends the floor's position and angle, the same values its own floor drawing used. It sends the height
    and the colours (with the fade) only when they change.
  - The Sub-CPU computes the chip's trace table, one entry per screen line, with the engine's own floor arithmetic
    (`Draw3DSkyLayer`). The chip then draws the floor (about 13 ms).
  - The 68000 copies it into one of two video RAM buffers, then shows that buffer from the next vertical blank.
  - The 32X's colour 0 is see-through in the special stages, and the SH2 leaves the floor's rows in colour 0, so the
    Genesis floor shows under Sonic, the UFOs and the HUD.
- **If anything fails**, the SH2 draws the floor itself, as R4N did. This covers no file, a bad file, or no answer.
  It also covers a load that ends the floor in the middle of a stage.
- **When the stage ends** (op 7, or any load): Word RAM goes back to 1 Mbit mode, and the Genesis set-up returns to
  what the other scenes use.

**Also in the floor work:**
- The SH2s draw the sky layers only down to the floor's first row. Before, they drew all 224 lines, and the floor then
  covered the lower 100.
- The floor's far rows at the horizon run past the edge of the floor's map, where the engine's floor is see-through
  and shows the sky. The SH2 still draws those few rows itself, over the sky, as R4N did. The chip draws the rest.
- The chip draws every line of the floor. R4N's SH2 floor drew every other line twice, to halve its cost.

## The next special stage in the cart

**All 8 do not fit.**
- A special stage's image is 1.6 MB, and 1.9 MB for special stage 1, so all 8 need about 13 MB.
- The 16 MB cart is already fully used, as 8 pages of 2 MB:
  - pages 0-1: the engine, the current scene, the data pack and an act file;
  - pages 2-5: the act pages;
  - pages 6-7: the menu and the act files.
- The game's BONUS list is empty: there are no bonus rounds.

**What R5C does:**
- **When:** at an act scene's load, if page 0 does not already hold the next special stage, it is loaded into page 0.
  The next stage is `SpecialStage.ListPos`, with Sonic's or Tails' files as the act's.
- **Why page 0:** that is where a special stage has always run, and its image needs more than an act page holds. While
  you play an act, page 0 only holds the title or a menu.
- **Then:** the giant ring's special stage is two page switches and no disc reading, with no loading picture.
- **What replaces it:** the title, a title demo, Time Attack, or anything else shown from page 0. The next act scene
  loads it again.
- **The first act after power-on** (Palmtree Panic 1, already in the cart from R4L's boot preload) starts with no
  loading, as before.
- **Line 10:** `5CB6` while the special stage preloads.

**The notice:** while the next level is written to the cart (an act's other periods, the next act's start, the next
special stage), the loading picture shows **LOADING NEXT LEVEL INTO CART** under its bar. The display is black for
two frames while the line is drawn.

**BONUS:** the stage list menu shows PRESENTATION, REGULAR, SPECIAL (`engine/Debug.cpp`, `patches/engine.patch`).

## The boot stack

- **`cd32x/sub_main.c` gets one new SH2 command, 80.** Only the Sub-CPU can drive the graphics chip, so this needs a
  Sub-CPU command.
- **The 68000 listener (`kobo/hw_md.s`) is unchanged.** Command 80 uses its existing routines 7 (copy to video RAM),
  20 / 22 (writes) and 34 (VDP register).
- `kobo/sub_stubs.c`, `kobo/scd.c` and `sh2_engboot.c` (`SH2-VR4I`) are unchanged.
- While the floor is on, Word RAM belongs to the floor. Any command other than the pad, the display, CD audio and the
  backup-RAM words ends the floor first.

| Command 80, COMM2 low byte | What |
|---|---|
| 0 | begin: COMM2 high byte = special stage 1-8. Reply COMM12 = 1 on, 0 no floor |
| 1 | COMM8 = the floor's X << 16 \| Z, 13.3 each |
| 3 | COMM8 = its height (YPos) |
| 4 | COMM8 = sin << 16 \| cos of its angle: draw this picture's floor. Reply COMM12 = 1 on, 0 ended |
| 5 | COMM2 high byte = colour k (1-15), COMM8 = the Genesis colours k and k + 1 |
| 6 | COMM2 high byte = colour k: reply COMM8 = its palette index in the floor file |
| 7 | end |

## The changed files

| File | Change |
|---|---|
| `cd32x/sub_main.c` | command 80: the floor (Word RAM 2 Mbit mode, the chip, the window plane) |
| `cd32x/engine/plat_mars.cpp` | the SH2's side: floor values per picture, colour 0 see-through, the fall-back; the next special stage's preload; the loading notice |
| `patches/engine.patch` (`engine/Debug.cpp`) | no BONUS in the stage list menu (R4N's changes kept) |
| `cd32x/engine/scd_pm.h` | the sky layers stop at the floor's first row |
| `tools/build_floor.py` | new: `FLOOR1.BIN` – `FLOOR8.BIN` |
| `tools/sh2emu/sh2emu.c` | command 80 answered as the Sub-CPU does (the file's checks; the chip's picture is not drawn) |

## Speed (sh2emu, special stage 1, the last 10 s of a 50 s run)

| | pictures a second |
|---|---|
| R4N | 5.4 |
| R5A (the chip's floor) | 7.6 |
| R5C | RESULT_R5C |

ares runs the SH2s faster than the console does. In ares, R4N and R5C both show about 10 pictures a second, so ares
can show whether the picture is right but cannot measure the speed-up.
