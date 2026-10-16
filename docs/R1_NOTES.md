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
| `8xxx` / `7xxx`, changing | (R1C) the PC sampler, on between `5C16` and `5Lnn`: where the SH2 is (`8xxx` = engine code at `0x02000000 + xxx × 16`, look it up in `engine.map`) |
| `5100` | R11A (list 1, scene 0) loaded by the engine |
| `5C60` / `5C65` | (R1C) the MDG load started / the MDG in the cart, SH2 command 68 next |
| `5C61` | no `G1S000.MDG` on the disc (the build step was skipped) |
| `5C62` | the MDG file is not what the SH2 expects (header) |
| `5C63` | the Sub-CPU refused the load (header, or the file not in one 1 MB cart bank) |
| `5C64` | the 68000 did not take a record within about a second: planes off again |

A frozen picture with no line-10 change would point at the record handshake (COMM4 / COMM6).

## Command numbers (for the docs ledger)

| Where | Number | Meaning |
|---|---|---|
| SH2 → Sub-CPU | **68** | Genesis planes: COMM2 = 1 load (COMM8 = cart offset of an "SCMG" file), 2 off. Reply COMM12: 1 ok, 0 refused. |
| Sub-CPU → 68000 | none new | 68 uses the listener's existing commands (1 `md_init_hw`, 4 / 8 `clear_b` / `clear_a`, 7 `copy_vram`, 12 `set_palette`, 20–22 `read/write_32x`, 34 `set_vdp_reg`). |
| COMM4 / COMM6 | – | while the planes are on: record id (SH2 writes COMM4) / record taken (68000 writes COMM6; its heartbeat stops). |
| CMD 38 ids | `0x1100`–`0x14FF` | `G<list>S<pos>.MDG` → cart slot 28 (`0x380000`). |
| 68000 work RAM | `0xFF8000`–`0xFFAF22` | `SGB_FLAG`, `SGB_LAST`, `SGB_READY`, the record buffer. |

68 is free on every stack (Jazz REV and SuperTux REV both use 67).
