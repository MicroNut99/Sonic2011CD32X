# R3 – the sprites, and Tails

**Goal of the R3 disc:** Palmtree Panic 1 at full speed (R1X: 60 frames a second on the console) **with its
sprites** – Sonic, Tails, rings, badniks, item boxes, the HUD, the title card and the results – and **Tails
playable**: the act plays through as Sonic, then starts again as Tails, then as Sonic again, and so on.
As in R1, the scripted pad plays (spin dash, run right, one jump, every 4 seconds).

## The sprites: Genesis hardware sprites

The 1993 Mega-CD game drew its sprites with the Genesis VDP, and so does R3. The 32X layer stays off:

- The 32X frame buffer holds the engine's record area (12 KB), menus (34 KB) and stage layouts (82 KB), and SDRAM is
  full; a 32X picture would also mean swapping the two frame buffers every frame.
- The plane record the 68000 already copies every frame (`md/sgb.h`) can carry anything for the VDP: its write groups
  reach any VRAM address. So the sprites' patterns and the sprite table travel in it. **No 68000, Sub-CPU or
  boot-stack change** – the boot stack is R1X's.

| Step | Where | What |
|---|---|---|
| on the PC | `host/md_check.cpp` (`SCD32X_MDGEXPORT`, `tools/build_mdg.sh`) | every sprite frame of the act – the scripts' frames and the animation files' frames – becomes 4-bit cells on one palette line, cut into hardware sprites of up to 4 × 4 cells; frames the engine rotates get their quarter turns (the engine's own `DrawSpriteRotated` was run on the PC for each direction and turn: a cell set, the VDP's flips and an offset). The pack goes into the act file after the plane data (`md/md_sprites.h` has the format). |
| every frame | SH2 (`md/md_sprites.c`, from `plat_sprite` in the engine's draw calls) | each draw call finds its frame (a small cache, then the pack's hash table); `planes_frame` puts the frame's pattern uploads (cell sets not yet in VRAM, at most 768 words a frame; when VRAM is full, the run of neighbouring sets drawn the longest ago makes room) and the sprite table (`0xF800`) into the record, before the plane writes |
| vertical blank | 68000 (`sgb_vblank`, unchanged) | the record into the VDP, as in R1 |

- **Priority:** draw lists after the act's last front tile layer get the priority bit (in front of loops and tunnels);
  the others go behind the front tiles, as in the engine.
- **Colours:** the planes use palette lines 0-2; the sprites get line 3 (the 15 colours Sonic or Tails, the HUD and
  the rings use most) and the planes' free slots. A frame whose colours fit no line is drawn with the nearest colours
  of the best line ("remapped": a few shades differ).
- **Limits are the Genesis's own:** 80 sprites, 20 on a line, 320 dots a line. Palmtree Panic 1 needs at most 63.
- **See-through effects** (the shield, sparkles: the engine's blended draws) are drawn every other frame, the way
  Genesis games did it; nearly opaque ones plain, very faint ones not at all.
- **Not as sprites yet:** rotations other than quarter turns, scaling – later, maybe on the 32X layer.

## Tails

A scene played as Tails (the game's character choice, `playerListPos` 1) has its own two files, numbered 128 on:

| File | What | Made by |
|---|---|---|
| `L1S128.IMG` | Tails's scene image: the same fixed layout as every image, but with Tails's animation files and sprite sheets in the order his scripts load them | `tools/build_game_data.py --scenes ...,1:0@1` |
| `G1S128.MDG` | Tails's act file: the same planes, a sprite pack with Tails's frames | `tools/build_mdg.sh ... 1:0,1:0@1` |

The Sub-CPU already maps every file number in these ranges to a name (`L<list>S<pos>.IMG`, `G<list>S<pos>.MDG`),
so no Sub-CPU change. `SCDDATA.PAK` holds the files of both.

**The restart** (`cd32x/engine/plat_mars.cpp` `plat_stage_loading`): when the engine asks for a scene that is not on
the disc – the act's end (where R1X stopped: `NO SCENE 204`), a time warp, a special stage – the SH2 loads the start
scene again with the other character, and the engine loads the whole stage again (its scripts load the other
character's files). Sonic → Tails → Sonic ... Score and lives carry over. Each switch loads the other scene image
(60 chunks) and act file (13-14 chunks) from the CD, one 32 KB chunk a second as at start-up: **about 75 seconds of
loading text** between the acts.

## Checked before burning

- **PC model** (`SCD32X_SGBTEST=1`, `SCD32X_DISC_SCENES=1:0`, `SCD32X_PAD=demo`): 13,000 frames – Sonic's act, the
  switch, Tails's act. Planes through the record + 68000 model: 0 pixels different. The whole picture (planes and
  sprites) against the engine's: 12,784 frames compared, 3,195 with differences – remapped shades (median about 100
  pixels a frame) and the see-through effects (up to about 2,300 pixels: the shield, sparkles).
- **sh2emu** on the R3 build with the disc's files, 16,500 frames (275 s): Sonic's act, results, `L1S004.IMG` not
  found, `L1S128.IMG` and `G1S128.MDG` loaded, Tails's act, results, `L1S132.IMG` not found, Sonic again.
  `VERDICT no problems found`. Its VDP model now draws the sprites (`-shot`).
- **Speed** (sh2emu `-profile`): 59.9 frames a second; an engine frame takes 0.65 vertical blanks on average (R1X:
  0.49), 9 in 10 under 0.76; the slowest frames about 1.0 (new plane rows and sprite patterns in the same
  record) - 1 or 2 in 4,000 just over one vertical blank. The sprites cost about 0.16 a frame: finding the frames, the
  sprite table and the patterns in the record, and the 68000's copy of those words.

## Build

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
rm -rf engine && sh tools/setup_engine.sh        # engine/ = RSDKv3 decompilation + patches/engine.patch (R3)
make -C host clean && make -C host               # clean: host/Makefile does not list md/md_sprites.* as inputs
rm -rf game_data
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk \
    /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data --scenes 0:0,1:0,1:0@1
# R1Q: L0S000.IMG must be Palmtree Panic 1 (Sonic), L1S000.IMG the title - build_game_data.py writes them the other way
mv game_data/L0S000.IMG game_data/swap.tmp && mv game_data/L1S000.IMG game_data/L0S000.IMG && mv game_data/swap.tmp game_data/L1S000.IMG
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk \
    /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data 1:0,1:0@1
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk 1:0
rm -rf cd32x/build_engine                        # also: cd32x/Makefile does not list md/md_sprites.* for plat_mars.o
make -C cd32x ENGINE=1 ROUND=R3A
```

Then `tools/sh2emu` (its README): `-f 7000` reaches the switch to Tails (about frame 6100).

## What to look for on the console

1. Boot and loading as R1X (`BUILD: SONIC CD R3A GAME START`), then Palmtree Panic 1 – now with Sonic, the rings,
   the badniks and the HUD.
2. The act plays through as before (about 80 seconds), "SONIC GOT THROUGH", the bonus count.
3. Instead of `NO SCENE 204`: the green text with `ERR: L1S004.IMG NOT FOUND`, then `OPENING L1S128.IMG...` and the
   chunk lines (about 75 seconds with the act file), then the title card – with Tails – and Tails's act.
4. After Tails's act: `L1S132.IMG NOT FOUND`, `L0S000.IMG`, and Sonic again.

| What you see | Meaning |
|---|---|
| Sonic again instead of Tails (`L1S128.IMG NOT FOUND`) | Tails's scene image is not on the disc (`build_game_data.py` without `1:0@1`): the SH2 takes Sonic's |
| the loading text stays, line 10 `5C61` | no `G1S128.MDG` on the disc (`build_mdg.sh` without `1:0@1`): the game runs, the planes stay off |
| red `LAYOUT 280` | `L1S128.IMG` is not in the other images' layout (made by another `build_game_data.py` run) |

## Console log

| Disc | Result |
|---|---|
| R3A | **Works** (MicroNut99's videos, 16 Oct, 20:09-20:13): Palmtree Panic 1 with Sonic, rings, badniks, item boxes and the HUD as Genesis sprites; at the act's end `L1S004.IMG` not found, `L1S128.IMG` (60 chunks) and `G1S128.MDG` loaded, Tails's title card (score 5800 carried over) and **Tails's act**, shield flickering. Speed from the game's own clock: Sonic's act 0'02"03 -> 0'31"00 in 29.0 s of video, Tails's 1'18"31 -> 1'51"26 in 33.0 s: **59.9 frames a second**. During the switch the Sub-CPU prints `SH2 NEVER EVEN REACHED THIS FILE`: its start-up diagnostic, which sees the last record number still in COMM4 once the planes are off - harmless. Boot stack untouched. |

