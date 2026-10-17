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
| later: effects the VDP has no form for (rotation, scaling, translucency, more colours) | 32X | the 32X layer is off so far |

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
| R4: the whole game's acts, the controller, the Level Select menu, music and sound | **In sh2emu**: all 70 regular acts (every round, all four time periods), as Sonic and as Tails, with the controller, CD audio music and PCM sound effects, each scene's scripts as native code: 58-60 frames a second (Collision Chaos 2 past: 52) - next: the console. [docs/R4_NOTES.md](docs/R4_NOTES.md) |

## Rounds

| Round | Content |
|---|---|
| R0 | PC survey: every act through the Mega Drive plane model (`tools/survey_md.sh`). |
| R1 | The level on the Genesis planes, driven by the engine: the act's Genesis file (`G<list>S<pos>.MDG`) in the cart; each frame the SH2 streams both planes from the engine's own layer scroll (`md_planes.c`, proven in S3a) into a record; the listener copies it into the VDP in vertical blank. 32X layer off, scripted pad. [docs/R1_NOTES.md](docs/R1_NOTES.md) |
| R2 | Animated tiles as pattern rewrites (R5xD, R6x, R8x need it – see R0); layout changes (broken walls, moved blocks). |
| R3 | The sprites as Genesis hardware sprites, through the plane record: Sonic, rings, badniks, HUD, title card, results. Tails playable: his own scene image and act file; the act restarts with the other character. [docs/R3_NOTES.md](docs/R3_NOTES.md) |
| R4 | Every regular act as Sonic and as Tails, the controller, the Level Select menu (stage select, demo pad and debug mode switches, credits), music as CD audio tracks, sound effects on the PCM chip, each scene image with its own data pack and native scripts, scene changes that load only the chunks that differ. [docs/R4_NOTES.md](docs/R4_NOTES.md) |
| later | Water (palette per line: the 68000's line interrupt), animated tiles (R2's list), the 22 acts that differ in R0, special stages (32X, rotating floor), the title and story screens, the stages' own sound effects, time travel without loading (all four periods of an act fit the cart together – S2), 32X effects (rotation, scaling, translucency). |

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
the disc's steps are in [docs/R4_NOTES.md](docs/R4_NOTES.md) (R3: [docs/R3_NOTES.md](docs/R3_NOTES.md), R1: [docs/R1_NOTES.md](docs/R1_NOTES.md)). `SONICCD_HANDOFF.md` is the full log of rounds S1–S7.

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
