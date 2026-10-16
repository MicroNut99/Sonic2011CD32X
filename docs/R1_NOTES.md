# R1 – the level on the Genesis planes, driven by the engine on the SH2

**Goal of the R1 disc:** Palmtree Panic 1 (Present) on the Genesis planes, scrolling with Sonic's
camera. Both tile layers (the level and the per-line parallax background) are drawn by the Genesis
VDP. **The 32X layer is blank in R1 – Sonic, the rings and the HUD are not drawn yet** (that is R3).
Sonic is moved by the PC check's pad (spin dash, run right, every 4 seconds), so the camera moves
without anyone playing.

## How it works

| Step | Who | What |
|---|---|---|
| on the PC | `tools/build_mdg.sh` | runs the act in the host engine and writes `G1S000.MDG` (the act's Genesis data, `md/sgb.h` "SCMG": cells, palette slots, 16 × 16 tile name words, chunk table, start colours, a zero block) |
| scene loaded | SH2 (`cd32x/engine/plat_mars.cpp` `planes_load`) | loads `G<list>S<pos>.MDG` with CMD 38/46 into cart slot 28 (`0x380000`), then **SH2 command 68 / 1** with the cart offset |
| CMD 68 / 1 | Sub-CPU (`cd32x/sub_main.c`) | through the listener's existing commands: VDP registers (planes `0xC000` / `0xE000`, h-scroll table `0xFC00`, line h-scroll, 64 × 32, H40), planes cleared, the cells into VRAM and the colours into CRAM (68000 reads the cart through the 32X bank window `0x900000`), the record handling on (`SGB_FLAG`), display on. No Kobo text while the planes are on. |
| every frame | SH2 (`planes_frame`) | the engine's own layer scroll (`plat_layer_*` hooks in `DrawHLineScrollLayer`) goes through `md/md_planes.c` (the code proven against the engine in S3a); the frame's VDP work is written as a **record** (`md/sgb.c`) into the frame buffer's 12 KB record area; Kobo `give_fb`, record id in **COMM4** |
| | 68000 command loop (`sgb_poll`, `cd32x/kobo/hw_md.s`) | sees the new id (read twice), copies the record from the frame buffer (`0x840000`) into work RAM (`0xFF8010`), answers the id in **COMM6**; the SH2 waits for that answer (read twice) and takes the frame buffer back |
| vertical blank | 68000 (`sgb_vblank`) | name-table writes, vertical scroll, the per-line h-scroll table, palette changes into the VDP |
| scene change | SH2 → **CMD 68 / 2** | records off, the listener's own VDP set-up and font back (`md_init_hw`), so the loading lines show as before |

Only the SH2 switches the frame buffer's owner bit (Kobo's pattern, S4b lesson). While the planes are
on, the 68000 does not write its heartbeat into COMM6. Palette: the engine's colours behind each CRAM
slot are compared every frame and the changes go in the record (fades, colour cycles).

## Proved on the PC (no console needed)

`SCD32X_SGBTEST=1 sh tools/run_md_check.sh Data.rsdk <scene> 1800`: each frame the record is built
from the **MDG file's** tables (as the SH2 reads them), applied by a C model of the 68000 code
(`sgb_apply`) to a second VRAM model, and that picture is compared with the engine's own layers.

- Palmtree Panic 1 (R11A): **1,799 frames, 0 pixels different**. Records: 127 words a frame on
  average, 3,841 at most (the first frame: both planes whole) – the limit is 6,000.
- All 70 acts: every act whose layers the plane model already matched matches through the record
  too (R11C even becomes identical: the SH2 path follows the engine's live layout). Differences
  remain where R0 found them, plus the acts with **animated tiles** (R41A, R6x, R72C/D, R8x): the
  R1 file holds each tile's first picture only – round R2.
- Not in R1: **water** (a second palette bank on part of the screen; R11A has a little – those lines
  show the colours above the water), animated tiles, sprites.

## Not proved (no 68000 / SH2 compilers here)

- `cd32x/kobo/hw_md.s` additions (`sgb_poll`, `sgb_vblank`, `sgb_runs`, the three lines in `md_start`,
  the vertical-blank and command-loop hooks) are hand-written 68000 assembly – read carefully, never
  assembled here. Expect the assembler to have the first word.
- `plat_mars.cpp`, `sub_main.c`, `md/sgb.c`, `md/md_planes.c` compile with the PC's gcc (syntax);
  the SH2 build is yours.
- The engine's SDRAM: the R1 build adds about 3.5 KB (two scroll lists, two h-scroll lists, the plane
  state). The link prints "RAM end": it must stay below `0603A400`.

## Build

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
sh tools/setup_engine.sh                     # once: engine/ = RSDKv3 decompilation + patches/engine.patch
make -C host                                 # the PC engine (for the files below)
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk \
    /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data --scenes 0:0,1:0
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk \
    /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data 1:0
rm -rf /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine
make -C /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x ENGINE=1
```

The disc: `cd32x/build_engine/SCDENG.cue`. The game data folder needs the title scene (0:0) too: the
boot loader loads it first and the engine binds the scene image's layout from it.

Make options: `PLANES=0` = the S7e drawing build; `START="1 0"` = the scene the engine starts in;
`DEMO_PAD=0` = no scripted pad.

## What to look for on the console

1. Boot as before (white + green lines, `BUILD: SONIC CD R1 GAME START`), loading lines for the
   engine, the data pack, the title's and R11A's scene images, then `G1S000.MDG`.
2. The text disappears and **Palmtree Panic 1 appears on the Genesis**: ground, loops, the parallax
   background. No Sonic (32X layer off).
3. After about 4 seconds the picture starts to scroll right, in bursts every 4 seconds (the spin
   dashes), with the background at its own speed.

While the planes are on, the Kobo text is hidden (it would print into the planes). If the planes do
not come, line 10 tells why:

| Line 10 shows | Meaning |
|---|---|
| `5C14` / `5C15` / `5C16` | (R1C) the engine's scene load: the scene in the cart / its image checked / the engine's own stage load running |
| `58nn` | only with `SCD_SCENE_CHECK=1`: the engine's check of the new scene image, reading at `0x080000 + nn × 64 KB`. R1C died in it (`5C14` → `101`); R1D leaves it out, as OpenJazz REV does |
| `6nnn` between them, CKPT `8xxx` | (R1E) the sampler's count (line 10 keeps changing while the SH2 takes interrupts); CKPT = the PR sampled with the PC (the caller, coded the same way) |
| `8xxx` / `7xxx`, changing | (R1C) the PC sampler, on between `5C16` and `5Lnn`: where the SH2 is (`8xxx` = engine code at `0x02000000 + xxx × 16`, look it up in `engine.map`) |
| `5100` | R11A (list 1, scene 0) loaded by the engine |
| `5C60` / `5C65` | (R1C) the MDG load started / the MDG in the cart, SH2 command 68 next |
| `5C61` | no `G1S000.MDG` on the disc (the build step was skipped) |
| `5C62` | the MDG file is not what the SH2 expects (header) |
| `5C63` | the Sub-CPU refused the load (header, or the file not in one 1 MB cart bank) |
| `5C64` | the 68000 did not take a record within about a second: planes off again |

A frozen picture with no line-10 change would point at the record handshake (COMM4 / COMM6).

## Fast loading (R1J)

The boot loader loads the scene the engine starts in (`FILE_FIRST_SCENE` in `cd32x/sh2_engboot.c`,
0x200 = `L1S000.IMG`; it must match the Makefile's `START`), so the engine does not load a second
1.9 MB image at once. A scene image already in the cart from an earlier run is not copied again: the
Sub-CPU compares its first, middle and last chunk with the disc (command 47, OpenJazz REV's quick
check) - line 10 `5E00`-`5E02` while it compares, `5E0F` = skipped. **Hold A+B+C** while it boots to
copy it anyway. The engine, the data pack and the MDG are always copied (they change with every
build). The loader's whole-image check still runs at boot.

## Command numbers (for the docs ledger)

| Where | Number | Meaning |
|---|---|---|
| SH2 → Sub-CPU | **68** | Genesis planes: COMM2 = 1 load (COMM8 = cart offset of an "SCMG" file), 2 off. Reply COMM12: 1 ok, 0 refused. |
| Sub-CPU → 68000 | none new | 68 uses the listener's existing commands (1 `md_init_hw`, 4 / 8 `clear_b` / `clear_a`, 7 `copy_vram`, 12 `set_palette`, 20–22 `read/write_32x`, 34 `set_vdp_reg`). |
| COMM4 / COMM6 | – | while the planes are on: record id (SH2 writes COMM4) / record taken (68000 writes COMM6; its heartbeat stops). |
| CMD 38 ids | `0x1100`–`0x14FF` | `G<list>S<pos>.MDG` → cart slot 28 (`0x380000`). |
| SH2 → Sub-CPU | 47 | (R1J, as OpenJazz REV) compare a chunk with the cart, nothing written; reply COMM12 0 = the same. |
| 68000 work RAM | `0xFF8000`–`0xFFAF22` | `SGB_FLAG`, `SGB_LAST`, `SGB_READY`, the record buffer. |

68 is free on every stack (Jazz REV and SuperTux REV both use 67).

## Console log

| Disc | Result |
|---|---|
| R1 | 5CEA at PC 06000851: the loader routine's address was in boot record [4] = the vertical-blank counter. Fixed in R1B ([6]). |
| R1B / R1C | stopped after the engine's L1S000 load: line 10 `101`, CKPT `0000` (R1C's short clips missed the steps in between). |
| R1D | the engine's image check passed (`5800`-`581A`), then `8F35` (FileRead) for ~8 s, then `101`. |
| R1E (no image check) | **the engine loaded R11A** (sampler `8F36`, `95D8`, `95EB`), `G1S000.MDG` loaded (4 chunks OK), SH2 command 68 ran (text off, screen blank), then a 68000 exception - printed in level tiles, unreadable. R1F prints it with the font. |
| R1F | readable: `68K EXC V0B` (line-F) at PC `00FF10EA` = `cmd_table`, return address `00FF10DC` (after the dispatch `jsr`). `sgb_poll` left the record id in d0; the command loop loads only d0's low byte, so from id 256 on the dispatch went to the table itself. Before it: ~256 records taken, nothing visible. R1G keeps d0; a backdrop turned **red** = the 68000 got ids with no valid record behind them. |
| R1G | no crash, no red; a regular grid of dots, one per 8 × 8 cell, over the whole screen instead of the level. Later: real R11A pieces (grass, red triangles) drawn in rectangles among the dots, orange dots in one area (blank cells with another palette line: the dot is in VRAM cell 0), and the red 32X box `NO SCENE 204` (the engine asked for list 1 / position 4 - not on the disc - and the planes were not switched off: SH2 command 68 / 2 never ran). R1H (diagnosis disc, `SGB_DIAG` in `sub_main.c`): cells and colours not loaded, records taken but not applied, rows 12-13 show the last id and the 68000's copy of the record. |
| R1H (diagnosis) | rows 12-13 for 30 s: `SGB ID:1234`, `ST:0001`, `REC 0004 0004 0006 0004 0004 0000 0000`, clock running. COMM4 still held the Sub-CPU's start-up check **0x1234**: the 68000 took it as a record id, copied frame-buffer junk (length 4, 4 groups, address 0x0006 - R1G's dot in VRAM cell 0; R1F's crash: d0 = 0x1234). The SH2 never wrote an id in 30 s: planes_frame sent nothing. R1I: COMM4 = 0 at plane set-up; the PC sampler on while the planes are on (row 12 `ST` / line 10). |
| R1H, ~7.5 min later | `OPENING L1S004.IMG`, red `NO SCENE 204`, `SGB ID` still `1234`: the engine ran act 1 to its end with the scripted pad and asked for list 1 / position 4 - and never sent one record in all that time. R1J (replaces R1I, not burned): planes_frame posts on line 10 why it does or does not send - `Bxxx` reasons, `Cnnn` record nnn sent. |
| R1I, R1J | both stopped after the scene load with `STATE:101 32XReg:101 CKPT:0000` again (R1J: the loader's own load of L1S000; R1I unchanged from R1H up to that point). The stall is intermittent - R1E-R1H got through. R1K: rows 17 / 27 keep the last 8 STATE values with their times. |
| R1L | back to R1H (the last disc that reached the game) + only: COMM4 cleared at plane set-up (Sub-CPU, runs only in command 68), the `Bxxx`/`Cnnn` reason codes (planes_frame only), the line-10 history on rows 17 / 27 (Sub-CPU text). R1I's sampler changes and R1J's loader changes (fast loading, L1S000 as the first scene) are out. |
| R1M | **Boot stack frozen at R1H**, except one constant (MicroNut99: stop loading it twice): the loader loads `L1S000.IMG` (0x200) instead of the title, so the engine does not load a second 1.9 MB image. (SH2 loader, Sub-CPU loading, 68000 boot: unchanged from here on). R1H + only: the `Bxxx`/`Cnnn` codes (engine, after the level is up) and COMM4 cleared in command 68 (after loading). R1L's history rows are out (extra Sub-CPU prints during loading). |
