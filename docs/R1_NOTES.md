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
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk 1:0   # R1U: native scripts
rm -rf /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine
make -C /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x ENGINE=1
```

`setup_engine.sh` needs `engine/` gone first (`rm -rf engine`), and it rebuilds `engine/` without the native
scripts: run `build_native.sh` after it. Without `engine/ScriptNative.inc` every script is interpreted, as before R1U.
From R1Q on, the disc's `L0S000.IMG` is Palmtree Panic 1 and `L1S000.IMG` the title: `build_game_data.py`
writes them the other way round, so swap the two files after it.

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

## Before burning: tools/sh2emu (from R1R)

Every build goes through the SH2 emulator first (`tools/sh2emu/README.md`): it boots the build folder's own loader and
engine with the disc files and reports what would hang or corrupt the console.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o tools/sh2emu/sh2emu tools/sh2emu/sh2emu.c
tools/sh2emu/sh2emu cd32x/build_engine > sh2emu_log.txt; tail -25 sh2emu_log.txt
```

Burn only on `VERDICT  no problems found`. The 68000 and the Genesis VDP are models (`-shot FRAME FILE.bmp` saves the picture).

## Speed (from R1S)

R1S runs Palmtree Panic 1 at **27-29 engine frames a second**; Sonic CD's logic runs once a frame and is made for 60,
so the game runs at half speed. Measured with `tools/sh2emu` after two fixes to its timing (it now charges the cart for
instruction fetches and models the 68000's record copy), calibrated against the R1S console video: stage load 2.5-2.8 s
(console: 2.5-3.0 s), and the same picture as the console within 0.1 s through the loop and the climb into the sky.

`sh2emu -profile 1300 2500` on R1S, per engine frame (one vertical blank = 383,500 SH2 cycles):

| | vertical blanks |
|---|---|
| busy, mean / median / slowest tenth / max | 1.55 / 1.46 / 2.30 / 2.56 |
| scripts (`ProcessScript` and what it calls), mean / slowest tenth | 0.57 / 1.38 |
| layer scroll (`DrawHLineScrollLayer`) | 0.29 |
| object loop (`ProcessObjects`, its own time) | 0.22 |
| plane record + the 68000's copy (`planes_frame`, `md_plane_update`, ...) | 0.17 |

- About 45% of the busy time is the SH2 waiting for the cart: the engine's code, the script bytecode and the tables
  live there (a cache line from the cart: ~88 cycles; from SDRAM: 12).
- The slow frames are the **3D ramp's player script** (type 66 `3DRamp`, player event): a 128-turn loop, ~1,070
  script instructions, 0.9 vertical blanks a frame while Sonic is near it. The interpreter costs ~320 cycles a script
  instruction: its 31 KB in the cart, every operand through two jump tables whose targets miss the cache.
- 60 frames a second needs every frame under one vertical blank.

**Native scripts (R1U).** `tools/scd_aot.py` translates the scene's script bytecode into C++ at build time
(`engine/ScriptNative.inc`, made from the game's data - not in the repository): each instruction becomes the
interpreter's own statements for it (operand reads, the instruction's `case` body, the write-back), taken from
`ProcessScript` after the preprocessor, so only the dispatch disappears; if / while / switch become gotos resolved
from the jump table. The engine runs a sub natively only when the loaded bytecode is exactly the one translated (a
hash over the script code, the jump table and the function table, once per stage load); otherwise it interprets.
R11A: all 231 object subs and 49 functions native (10,049 instructions, 198 KB of SH2 code in the cart).

**Hot code in SDRAM (R1V).** A cache line of code from the cart costs 87 cycles, from SDRAM 11. After R1U a third of
the busy time was the cart's code fetches. Functions marked `SCD_HOT` (`engine/RetroEngine.hpp`; `SCD_MD_HOT` in
`md/`) go into the section `.sdtext`: linked to run in SDRAM, stored in the cart image after `.data`, copied at engine
start like `.data` (`cd32x/engine/engine_cart.ld`, `engine_start.cpp`), then the cache is purged. In it: the object
loop, the native-script dispatch, the plane record code, the collision tests the player runs every frame, the player
control and camera, the native subs listed in `tools/build_native.sh` (`HOT=`: the player, rings, stage set-up...) and
libgcc's division and shift helpers - about 17 KB. The native dispatch reads a table in SDRAM, not the cart.

**R1W.** Waste the R1V profile showed, each with the same result:
- `ProcessObjects` skips each run of empty slots in one tight loop (`objectLoop` is read by nothing in between).
- A script sub that is not the running object's own (an object that changed its type in its main sub still gets the
  player sub of the type it had: Palmtree Panic 1's crumbling ledge pieces) ran in the interpreter - 87,000 cycles in
  one frame. Now the native code made from exactly that bytecode runs (found by its start and its jump table).
- The foreground's line positions: every `linePos` entry is the same number there, so with no line deformed (no
  `hParallax.deform` flag set at the stage load: `scdHDeformAny`) every line gets that number - no frame-buffer read
  per line. Only the background reads its 224 scroll bytes.
- `DrawHLineScrollLayer`'s OpenGL half (never taken on the console) is a function of its own; the half that runs went
  into SDRAM.
- The plane rows: the layout and chunk entries read once per chunk / per tile instead of per cell, and each row
  piece goes into the record in one run (`sgb_write_run`: the same words as the per-word writes - same record CRC).
- The plane window moves 2 columns past the needed ones instead of re-centring (`MD_SLACK`): the same picture, the
  new columns a few at a time (records differ from R1V; pictures identical - PC record model and sh2emu screenshots).
- The master stack's reservation 12 KB → 8 KB (the engine's deepest use is 3.9 KB); the 4 KB went to `.sdtext`.
- `FlipScreen` no longer computes the PC's idle-dim timer on the console (only the OpenGL / SDL paths draw it).

**R1X.**
- Live bits for the object slots (`scdLive`, `engine/Object.hpp`): one bit per slot, set wherever a slot gets a type
  (a script's `object.type` write, `ResetObjectEntity`, `CreateTempObject`; rebuilt from the types after a stage
  load), cleared when `ProcessObjects` finds the slot empty. A word of clear bits passes 32 empty slots unread. The PC
  builds check the rule every frame (a slot with a type has its bit): 18 acts x 2,400 frames, no miss.
- Shared functions for big script instructions (`tools/scd_aot.py`, `OUTLINE_TOKENS`): an instruction whose code is
  60+ tokens is one function (`scdop_<name>`) with the same statements and locals, called by every sub that uses it,
  instead of a copy in each - the native code ~200 → ~150 KB, the HUD's draw sub 5.2 → 1.2 KB (now in SDRAM).
- The SDRAM set by measured value (sh2emu's `code in SDRAM` list: cycles saved per byte).
- The background's lines without deformation: no deform test, one wrap per run of equal entries; the foreground's
  second slot does not refill `linePos` or its line table when they already hold the same number; the plane rows one
  tile at a time; a line with the previous line's x takes its scroll.

| sh2emu, Palmtree Panic 1 | R1S | R1T | R1U | R1V | R1W | R1X |
|---|---|---|---|---|---|---|
| frames a second | 23-27 | 29.1 | 43.7 | 59.8 | 59.9 | 59.9 |
| busy per engine frame, mean (vertical blanks) | 1.55 | 1.31 | 0.97 | 0.73 | 0.58 | 0.49 |
| slowest frame | 2.56 | | 1.28 | 1.09 | 0.82 | 0.73 |
| frames that fit in one vertical blank | 0% | | 63% | 99% | 100% | 100% |

The slowest frames now are the vertical scroll ones (new plane rows: ~300 name words), whose record the 68000 copies
for up to 0.1 vertical blanks while the SH2 waits (`planes_frame`), and the object loop (0.09). The copy is the 68000's
`move.w` loop in `sgb_poll` (75 SH2 cycles a word): twice as fast, it would take the slowest frame from 0.82 to 0.77
(sh2emu `-k68word 38` on R1W).

Speed rounds, one change per disc, every one checked before burning: the PC check (`tools/test_sh2cfg.sh` with
`REF=`: camera, layer scroll, palettes and every object's state the same as the previous round, frame by frame) and
sh2emu's `record contents` CRC (the 68000 gets the same records = the same pictures).

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
| R1M | the loader loaded `L1S000.IMG` itself (one load instead of two) - all 59 chunks OK, then `STATE:101 32XReg:101 CKPT:0000`, as R1J. 2 of 2 discs with the loader loading Palmtree Panic died there; R1E-R1H (loader loads the title, engine loads Palmtree Panic, no image check of it) all got through. R1N: the loader exactly as R1H again (title first); the double load stays until this is understood on the PC side. |
| R1N | stopped at `STATE:101` after the L1S000 load. R1H rebuilt unchanged (same disc label R1H): **got through** (rows 12-13 as before). So R1N's two changes - which do not run before that point - made the difference: the stall depends on how the code is laid out, a latent fault on the game side. From here: one change per disc against R1H. |
| R1O | (not burned - dropped) |
| R1P | **R1G exactly** (the last disc with level graphics; R1H/R1O were text-only diagnosis discs) + one engine change: the SH2 clears COMM4 before switching the planes on. |
| R1P | R1G + the SH2 clearing COMM4 (one statement, reached only after the stop): stopped at `STATE:101` after the L1S000 load, twice, 10 minutes each. |
| R1Q | **R1G + no double load**, nothing else. Boot stack R1G exactly. Game data: the disc's `L0S000.IMG` is Palmtree Panic 1's image (copied from `L1S000.IMG`); the engine treats its start scene as file 0x100, so it finds it already in the cart. One 1.9 MB scene load instead of two. |
| R1Q | one load, all 60 chunks OK, `5C03` → `5C11` → `5C16`, one sample `8F33` / CKPT `9550` (FileRead, called from LoadBytecode), then line 10 **froze** (no `6001` half a second later: the SH2 stopped taking its vertical-blank interrupt), `101` about 9 s later. **Found on the PC** with the R1Q build itself in `tools/sh2emu`: the same first sample (`8F34` / `9550`), then from frame 502 the engine runs **the cart image's sprite pixels as script code**. On the SH2, `scriptCode` is the scene image's script part (R11A: 0xE304 entries); the "no script" index `SCRIPTDATA_COUNT - 1` (0x3FFFF) lies in the image's graphics there (`0x08090900`, not 0), so `ProcessStartupObjects` / `ProcessObjects` / `DrawObjectList` ran garbage for every object type without that sub, reading at wild addresses (`0x2348B304`, `0x4A4CFC90`, `0x8E4BB728`...). Every build that ran R11A did this; where the reads land depends on where the engine's tables lie - the likely reason it killed some builds and not engine A. |
| R1R | **R1Q + one change** (engine): the six "has this object sub a script?" tests skip the no-script index (`ScriptSubExists`, `engine/Script.hpp`; `engine/Object.cpp` ×5, `engine/Drawing.cpp` ×1 - the PC's answer is unchanged). Boot stack R1G exactly, game data as R1Q. `tools/sh2emu` with this fix simulated on R1Q: stage loaded (`5100`), `G1S000.MDG`, command 68, records sent, no wild reads in 3000 frames. The COMM4 clear (R1P) is not on this disc: the 0x1234 junk record / dot grid may still show. |
| R1R | **ran the whole act, twice** (boot stack untouched): line 10 kept changing after `5C16` (`8F39`/`9554`, `6001`, `95EE`, `6002`), `G1S000.MDG` loaded, the planes came on. Then a dot in every blank cell, as R1G; ~5 minutes later the end of the act - red `NO SCENE 204` (the next act is not on the disc) - with a few rectangles of real level among the dots. `tools/sh2emu -shot` on the same build: the SH2's records draw Palmtree Panic 1 correctly (palm trees, grass, checkerboard, mountains) - plus the same dots from the junk record (COMM4 `1234` taken at planes-on). So the SH2 side is right; what is missing on the console happens after the hand-over. Every disc that reached the planes (R1F, R1G, R1R) took that junk record first. |
| R1S | **R1R + one line** (engine, R1P's): the SH2 clears COMM4 before command 68, so the 68000 never takes the start-up value as a record. sh2emu with this line simulated on R1R: no junk, the full picture, no dots. If the console still shows no level, the fault is in the 68000's record handling (`sgb_poll` / `sgb_vblank` in `hw_md.s`, the frozen stack) - stop and ask before touching it. |
| R1S | **Palmtree Panic 1 on the Genesis planes, scrolling with the engine** (MicroNut99's videos, 16 Oct): palm trees, mountains, checkerboard, the loop with the water behind it, the climb into the clouds - no dots, no junk. Slow: 27-29 engine frames a second (see *Speed*). **Last good disc.** |
| R1T | **R1S + three engine speed changes, nothing visible changes** (one disc for the three: each alone stays under the console's 30-frame step). (1) `CheckKeyPress` looked up `input.pressButton` by name every frame - a mobile variable Sonic CD does not have: 109 name compares from the cart a frame, for nothing; now searched once per variable list. (2) `ProcessObjects` skips an empty slot at once (~860 of 1,184; the only write it could do, `type = 0`, stores the 0 it holds). (3) `DrawHLineScrollLayer`, no-draw build: only each line's position, written straight into the plane record's table (`plat_layer_lines`), the scroll byte read once, no special-stage name compare per line (it needs `RENDER_HW`); the parallax `%` only for values outside the width. PC check: 3,001 frames the same as R1S. sh2emu: records 1-1000 CRC `CCFAE2B9` = R1S's; busy per engine frame 1.55 → 1.31 vertical blanks; 26.5 → 29.1 frames a second over 44 s of play. Boot stack untouched. |
| R1U | **R1T + native scripts** (above) **+ the scene image keeps the scripts' last instruction**: `host/cart_build` trimmed every part after its last non-zero element, and the last script's last instruction, `End`, is a 0 - so on the SH2 the end of Attract Mode's setup (run at every stage load) read the jump table's first word (76) as an instruction and went on with garbage (harmless so far: R1R/R1S ran). The host now records how much code the stage uses (`SPOS`) and the image keeps all of it (the native scripts need it too: their hash). PC check: 12,001 frames identical to the interpreter (camera, scroll, palettes, every object's state). sh2emu: records 1-2000 CRC `3235C173` = R1S's; 94 s of play: **43.7 frames a second** (R1S 23.4); a frame 0.97 vertical blanks on average (R1S 1.55), 63% of frames already under one; the stage loads in 1.0 s. Needs the game data rebuilt (the fixed images). Boot stack untouched. |
| R1V / R1W | (not burned yet - one disc with R1T, R1U and R1W: R1W) R1U + the hot code in SDRAM (R1V) + R1W's changes (*Speed*). PC check: 3,001 frames the same as R1S; PC record model 3,599 frames 0 pixels different; sh2emu: 59.9 frames a second, every engine frame under one vertical blank (slowest 0.83), `VERDICT no problems found`, records 1-2000 CRC `B2BF03A0` (R1W's plane window; the pictures are R1S's: 118 sh2emu screenshots identical to R1U's). Boot stack untouched. |
