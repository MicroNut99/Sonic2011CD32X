# Sonic CD (2011) for Sega CD + 32X – the rip and tear

Sonic CD's 2011 remaster (the RSDKv3 engine, from the RSDKv3 decompilation) on a Sega Genesis /
Mega Drive with **Sega CD**, **32X** and MicroNut99's **4 MB RAM cart**, on the Kobo / OpenLara
CD32X boot stack.

This repository is the **REV line**: the "Sonic CD rip and tear" method that took Jazz Jackrabbit
CD32X from 10–13 to 20–22 fps and SuperTux CD32X to a hitch-free 38–56 fps, applied to the game it
is named after. The earlier line (rounds S1–S7, `SONICCD_HANDOFF.md`) had the engine running from
the cart on the console – the title scene starting, game logic at 30 fps with drawing off – but
drawing everything with the SH2s was far too slow.

> **No game data is included.** You need your own `Data.rsdk` from the Steam release of Sonic CD
> (2011). The engine source is not included either: `patches/engine.patch` is applied to the
> RSDKv3 decompilation (see *Building*).

## The method

| Layer | Drawn by | How |
|---|---|---|
| far background (per-line parallax, water ripple) | Genesis plane B | the act's patterns in VRAM, h-scroll per line |
| level (the foreground layer; its front tiles with the priority bit) | Genesis plane A | the act's patterns, scrolled with the camera |
| Sonic, Tails, badniks, rings, HUD (R3) | Genesis sprites | the act's sprite frames as patterns, made on the PC; uploaded and placed by the SH2 through the same record |
| the sky behind the level (R4B); the loading picture; the scenes the planes cannot show (R4C) | 32X | R4B: a gradient per act where the background shows its sky colour, behind the Genesis planes; the title screen with a loading bar while the Sub-CPU loads. R4C: the **32X picture mode** - the title, the menus, time attack and the special stages drawn by the SH2 in 256 colours (rotation, scaling, translucency, the 3D floor), the Genesis display off |

- **The SH2 runs the 2011 engine** (scripts, physics, objects) from the cart, as before.
- **The Genesis VDP draws the level for free**, line by line, and scrolls it at 60 Hz – the way the
  1993 Sega CD original did. The 2011 stage art *is* the original Mega-CD art (S2 inventory:
  21–42 colours per act's tiles, nothing lost in 9-bit Genesis colour).
- **The SH2 works out what changes, the 68000 only copies it.** S3b showed the 68000 too slow
  for the plane maths (17 fps); so the engine's own scroll goes through `md_planes.c` on the SH2,
  and the 68000 copies the frame's record (new cells, scroll table, colours) into the VDP in
  vertical blank. The conversion of the art is done on the PC.
- **The sprites are Genesis hardware sprites (R3)**, as in the 1993 game: the SH2 puts the frame's new sprite
  patterns and the sprite table into the same record (no 68000 or boot-stack change). The 32X frame buffer holds the
  engine's layouts and menus, so a 32X sprite layer would need more memory than the SH2 has; it is for later effects.

Why it fits: RSDK tiles are 16 × 16 = exactly 2 × 2 Genesis patterns on the 8-pixel grid, and
every act's layers are one foreground and one per-line-scrolled background – two Genesis planes.

## Where it stands

| What | Status |
|---|---|
| Every act converted and checked against the engine on the PC (R0) | **done** – [docs/R0_SURVEY.md](docs/R0_SURVEY.md): 68 of 70 acts; 332–801 patterns per act (room for 1,535); 2–4 palette lines; 0 layers the VDP cannot show; 46 acts identical frame by frame, 22 with small local differences to explain |
| Genesis planes streamed by the 68000 from SH2-made records | S3c / S4b: 60 fps in **Fusion** (a standalone 68000 program). Not yet run on the console. |
| The 2011 engine on the SH2 from the cart | proven on the console in rounds S5–S7 (title scene starts; 30 fps with drawing off) |
| R1: the engine drives the planes (records through the frame buffer, the listener copies them in vblank) | **On the console (R1X): Palmtree Panic 1 on the Genesis planes at 60 frames a second** (measured on MicroNut99's video, 16 Oct), the whole act to its end. R1S was 27-29; the speed rounds R1T-R1X: native scripts, hot code in SDRAM, ... [docs/R1_NOTES.md](docs/R1_NOTES.md) (*Speed*). |
| R3: the sprites, and Tails | **On the console (R3A)**: Sonic, Tails, rings, badniks and the HUD as Genesis sprites at 59.9 frames a second; the act as Sonic, then as Tails. [docs/R3_NOTES.md](docs/R3_NOTES.md) |
| R4: the whole game's acts, the controller, the Level Select menu, music and sound | **On the console (R4A)**: every regular act as Sonic and as Tails with the controller, the music and the sound effects. [docs/R4_NOTES.md](docs/R4_NOTES.md) |
| R4B: the loading picture, the Options Menu, the pause menu, 32X skies, the time warp | **On the console (R4B)**: the title screen with a loading bar at power-on and between the scenes (no green text), the Options Menu (player, lives, debug mode), the pause menu (continue, restart, exit), a 32X sky behind 27 acts, the time warp to the future, the ramps run backwards. [docs/R4B_NOTES.md](docs/R4B_NOTES.md) |
| R4C: the game's own title and menus, the special stages, saves, faster loading, animated tiles | **In sh2emu**: power-on to the game's title screen, its main menu, save select and time attack, the special stages (drawn by the 32X), the save files in the Sega CD's backup RAM, up to four chunks a CD read, the animated tiles of 22 acts - next: the console. [docs/R4C_NOTES.md](docs/R4C_NOTES.md) |
| R4D: the second SH2, each stage's own sound effects, the underwater colours | **In sh2emu**: three Options Menu switches, each off at power-on (off: R4C) - `2ND SH2` (the 32X's second SH2 draws half of every picture of the picture mode), `STAGE SFX` (each scene's own PCM bank), `UNDERWATER` (Tidal Tempest's water colours from the 68000's line interrupt) - next: the console. [docs/R4D_NOTES.md](docs/R4D_NOTES.md) |
| R4E: the cart test for the 8 / 16 MB cart | **In sh2emu** (its model of MicroNut99's page mapper: 4, 8, 16 MB and a broken page): the Options Menu's `CART TEST` tries the pages and reports; the game as R4D - next: the new cart. [docs/R4E_NOTES.md](docs/R4E_NOTES.md) |
| R4F: the 16 MB game mode (`BIG CART`) | **In sh2emu** (`-cart 4 / 8 / 16`): the Options Menu's `BIG CART` row runs the cart test and, if it passes, keeps an act's time periods in the cart's pages - a warp to a period already in is a page switch, no CD; off at power-on (off: R4E) - next: the new cart. [docs/R4F_NOTES.md](docs/R4F_NOTES.md) |

## Rounds

| Round | Content |
|---|---|
| R0 | PC survey: every act through the Mega Drive plane model (`tools/survey_md.sh`). |
| R1 | The level on the Genesis planes, driven by the engine: the act's Genesis file (`G<list>S<pos>.MDG`) in the cart; each frame the SH2 streams both planes from the engine's own layer scroll (`md_planes.c`, proven in S3a) into a record; the listener copies it into the VDP in vertical blank. 32X layer off, scripted pad. [docs/R1_NOTES.md](docs/R1_NOTES.md) |
| R2 | Animated tiles as pattern rewrites (R5xD, R6x, R8x need it – see R0); layout changes (broken walls, moved blocks). |
| R3 | The sprites as Genesis hardware sprites, through the plane record: Sonic, rings, badniks, HUD, title card, results. Tails playable: his own scene image and act file; the act restarts with the other character. [docs/R3_NOTES.md](docs/R3_NOTES.md) |
| R4 | Every regular act as Sonic and as Tails, the controller, the Level Select menu (stage select, demo pad and debug mode switches, credits), music as CD audio tracks, sound effects on the PCM chip, each scene image with its own data pack and native scripts, scene changes that load only the chunks that differ. [docs/R4_NOTES.md](docs/R4_NOTES.md) |
| R4B | The loading picture (the SH2 loader and the engine: Sub-CPU commands 51 and 52), the Options Menu, the pause menu, 32X skies, the time warp, the layout changes on the planes, Palmtree Panic's palette banks, a restart without disc reads. [docs/R4B_NOTES.md](docs/R4B_NOTES.md) |
| R4C | The 32X picture mode (`cd32x/engine/scd_pm.h`): the title, the menus, time attack and the special stages; power-on to the title; saves in the backup RAM (Sub-CPU: Kobo's commands 56-59, a 512-byte file); faster loading (Sub-CPU command 46 with a run of up to 4 chunks, checked by the SH2); the animated tiles on the planes (`md_plane_refresh`). [docs/R4C_NOTES.md](docs/R4C_NOTES.md) |
| R4D | Three boot-stack changes behind Options Menu switches (off at power-on): the second SH2 for the 32X picture mode (the loader's `secondary()` runs the engine's jobs from a mailbox; `scd_pm.h` splits the floor, the tile layers, the big faces and the big sprites by rows), each scene's own sound effects (Sub-CPU command 53 with a bank number: `SFX<n>.BIN`), the underwater colours (the records' water trailer; the 68000's H-int at the water line). Fixes from the R4C disc: the title's demos (their button files in the acts' data packs), faster menus (half see-through pictures one pixel a word), short presses kept while a slow picture is drawn. [docs/R4D_NOTES.md](docs/R4D_NOTES.md) |
| R4E | MicroNut99's 8 / 16 MB cart (two 2 MB windows, 8 pages of 2 MB): the Sub-CPU's page switch (command 46, COMM2 `0xF8xx`, through the 68000's existing `SAFE_CART_XFER`), the Options Menu's `CART TEST`, sh2emu's `-cart` / `-cartbad`. The 16 MB game mode is next: [docs/CART16_HANDOFF.md](docs/CART16_HANDOFF.md). [docs/R4E_NOTES.md](docs/R4E_NOTES.md) |
| R4F | The 16 MB game mode: the cart as pairs of pages (pages 2k and 2k + 1 = a whole 4 MB cart as the game knows it: 16 MB four pairs, 8 MB two), an act's time periods each in a pair, a scene in a pair = both windows switched (window 0 too: each pair has its own verified copy of the engine); the Sub-CPU's command 46 with COMM2 `0xF0nn` (the opened file's cart slot); the Options Menu's `CART` row is `BIG CART` (the cart test, then on); sh2emu checks window 0's switches (`CART-W0`). [docs/R4F_NOTES.md](docs/R4F_NOTES.md) |
| later | The 22 acts that differ in R0, time travel without loading (all four periods of an act fit the cart together – S2). |

## Command numbers

New SH2 → Sub-CPU and Sub-CPU → 68000 commands take numbers that are free on every CD32X stack
(68 and up), and are added to the docs repository's command ledger. Note: Jazz REV (67 = Genesis
plane, 68000 37–40 = `genbg_*`) and SuperTux REV (67 = map columns, 68000 37 = `copy_vram_col`)
currently use the same numbers for different things.

## Building (PC side)

```bash
# the RSDKv3 decompilation (GitHub zip) unpacked here as RSDKv3-Decompilation-master/
sh tools/setup_engine.sh            # engine/ = original + patches/engine.patch
sh tools/check_patch.sh             # must say OK
make -C host                        # host/soniccd_host (no SDL; frames as PNG)
sh tools/run_md_check.sh Data.rsdk 0 1800    # one act through the plane model (0 = R11A)
sh tools/survey_md.sh Data.rsdk 1800 > survey.txt   # all 70 acts (R0)
SCD32X_SGBTEST=1 sh tools/run_md_check.sh Data.rsdk 0 1800   # R1: record + 68000 model (SGB lines in runs/scene0/events.txt)
```

The console build (`cd32x/`) needs the sh-elf / m68k-elf toolchain at `/opt/toolchains/sega`;
the disc's steps are in [docs/R4F_NOTES.md](docs/R4F_NOTES.md) (R4E: [docs/R4E_NOTES.md](docs/R4E_NOTES.md), R4D: [docs/R4D_NOTES.md](docs/R4D_NOTES.md); the bigger RAM cart: [docs/CART16_HANDOFF.md](docs/CART16_HANDOFF.md)) (R4C: [docs/R4C_NOTES.md](docs/R4C_NOTES.md), R4B: [docs/R4B_NOTES.md](docs/R4B_NOTES.md), R4A: [docs/R4_NOTES.md](docs/R4_NOTES.md), R3: [docs/R3_NOTES.md](docs/R3_NOTES.md), R1: [docs/R1_NOTES.md](docs/R1_NOTES.md)). `SONICCD_HANDOFF.md` is the full log of rounds S1–S7.

## Checking a build before burning: tools/sh2emu

[tools/sh2emu](tools/sh2emu/README.md) runs a build folder's own SH2 loader and engine with the disc files on the
PC (an SH2 emulator with the 32X's memory map) and reports anything that would hang or corrupt the console. It
ends with a verdict: burn only on `no problems found`. How to build it, run it and read its log:
[tools/sh2emu/README.md](tools/sh2emu/README.md).

## Credits

- Sonic CD © SEGA. The 2011 remaster by Christian Whitehead.
- RSDKv3 decompilation by Rubberduckycooly and st×tic (its own licence, `RSDKv3-Decompilation-master/LICENSE.md`).
- The credits screen's plasma (R4): OpenJazz's, © 2010 Alireza Nejati, GPL-2.0, as MicroNut99's
  Sonic32XPlus1-RealHardware draws it – see *Licences* in [docs/R4_NOTES.md](docs/R4_NOTES.md).
- The CD32X framework: Victor Luchits (viciious); Sega CD and 32X code: Chilly Willy.
- Boot stack: the Kobo / OpenLara CD32X framework (`KOBO_CD32X_BOOT_PROCESS.md`, `cd32x/kobo/README.md`
  and `cd32x/kobo/LICENSE.md` name its authors and licence).
