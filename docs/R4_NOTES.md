# R4 – the whole game's acts, the controller, the Level Select menu, music and sound

**Goal of the R4A disc:** every regular act of Sonic CD (Palmtree Panic to Metallic Madness, all four time
periods), as Sonic and as Tails, **played with the controller**, chosen from a **Level Select menu**, with the
game's **music** (CD audio tracks) and **sound effects** (the Sega CD's PCM chip), at 60 frames a second. Not yet:
the special stages, the title and story screens, animated tiles, underwater colours.

**Boot stack:** unchanged (R1X's). No new Sub-CPU or 68000 command: the pad (48), CD audio play / stop (45 / 54),
the sound effect bank (53) and the planes (68) were all there.

## The Level Select menu

The console boots into it (the engine's own developer menu, `engine/Debug.cpp`, drawn on Genesis plane A with the
game's 8 × 8 font – `G0S255.MDG`):

| Row | What it does |
|---|---|
| `START GAME` | Palmtree Panic 1 as Sonic |
| `STAGE SELECT` | the player (Sonic / Tails), then the stage list (`REGULAR` has every act), then the act. **Start** plays it; **A** plays it in debug mode |
| `DEMO PAD` (off) | on: the scripted pad of R1-R3 plays instead of the controller (spin dash, run right, every 4 s) – until any button is pressed in the game |
| `DEBUG MODE` (off) | on: every stage starts in the engine's debug mode |
| `CREDITS` | the credits screen (below) |

- **A + B + C + Start** during the game: back to the menu.
- A scene that is not on the disc – the title screen after a game over, a special stage (the big ring), the end of
  the game – brings the menu back with two lines: `NOT ON THE DISC: L<list>S<pos>.IMG` and the stage's name. Line 10
  shows `5C17`.

## The controller

Pad 1 through the existing path: the 68000 reads the pads in its vertical blank, the Sub-CPU answers SH2 command 48
with them in COMM8. One request a frame, answered while the engine runs (each frame uses the previous frame's
answer). A, B, C jump (a six-button pad's X, Y, Z too), Start pauses.

## Every act on the disc

| File | What | Made by |
|---|---|---|
| `L1S<pos>.IMG`, `L1S<pos + 128>.IMG` | each act's scene image as Sonic and as Tails – 140 files (`L0S000.IMG` = Palmtree Panic 1 as Sonic, the one the boot loader loads) | `tools/build_game_data.py --scenes 1:*,1:*@1 --native ...` |
| `G1S<pos>.MDG`, `G1S<pos + 128>.MDG` | each act's planes and sprites – 140 files | `tools/build_mdg.sh ... 1:*,1:*@1` |
| `G0S255.MDG` | the menu's font | `build_game_data.py` |
| `G0S254.MDG` | the credits | `build_game_data.py` |
| `KOBOSFX.BIN` | the sound effects (PCM) | `tools/build_audio.py` |

- **Smaller scene images:** the planes build never reads the sprite and tile pixels or the blend tables (image parts
  14-16), so they are left empty: about 1.1 MB an image (R3: 2.07 MB).
- **Each image carries its scene's data pack** (part 17: the files the engine reads for that scene), so no shared pack
  has to hold every scene's files. `SCDDATA.PAK` is only for the boot loader.
- **Only the chunks that differ load.** The image header has a CRC for every 32 KB chunk; at a scene change the SH2
  compares them with the image in the cart and has the Sub-CPU load only the chunks that differ (command 46 takes a
  chunk number).
- **Native scripts for every scene** (part 18): the engine has the global scripts as native code (R1U); each image now
  carries its own scripts as native SH2 code too (`tools/scd_aot.py --part stage`), compiled and linked for its place
  in the cart against the engine's symbols. The engine uses it only if it was made for this very engine (a checksum
  of `engine.bin`) and this scene's bytecode (a hash) – otherwise the scripts run in the interpreter, as before. So
  `build_game_data.py --native` must run after every engine build. Without it the scene's own scripts run in the
  interpreter: Palmtree Panic 1 at about 37 frames a second instead of 60 (sh2emu).
- **Quartz Quadrant 1 and 2 in the past** (`R51B`, `R52B`) had no act file: their tiles use 46 colours in 38
  different colour sets of an 8 × 8 cell, and the PC tool's quick packing of those into palette lines needed 5 - the
  Genesis has 4. A local search after it (`host/md_check.cpp` `pack_lines`, only when the quick packing needs more
  than 4) finds 4 lines of 15, 14, 15, 15 colours: the same picture as the engine's (PC check: 1,799 frames compared,
  0 different). With all four lines full, their sprites are drawn with the nearest colours of the level's lines (as
  R3 does for any frame without a line of its own: a few shades differ). Every other act's file is unchanged.

## Music and sound effects

**Music** – CD audio tracks 2-39, made from the game's own `.ogg` files (`tools/build_audio.py`, ffmpeg):

| Tracks | |
|---|---|
| 2-8 | the past (`R1B` ... `R8B`) |
| 9-29 | each round's present, good future, bad future (Japanese soundtrack: one soundtrack fits; the US names play the same tracks) |
| 30-35 | boss, final boss, invincibility, speed shoes, game over, zone complete |
| 36-39 | special stage, title, time attack, D.A. Garden – no scene plays them yet |

The Sub-CPU plays a track over and over (command 45) and stops (54); a track the game does not loop (zone complete,
game over) is stopped after its length. The commands go out in the pad request's place, not waited for. The game's
own scripts choose the music (the act's, the boss, invincibility, speed shoes, zone complete, game over); the engine
passes each choice on as a track number.

**Sound effects** – the game's 21 global effects (`Data/SoundFX/Global`: ring, jump, spin dash charge and release,
skid, spring, hurt, rings lost, destroy, explosion, lamp post, shield, the time posts and the time warp, sign post,
score tally, 1-up, Tails flying and tired) on the Sega CD's PCM chip, played by the Sub-CPU (Kobo's path, `KOBOSFX.BIN` loaded once by
command 53; the effect's number rides in the pad request). **Pitch:** the chip plays 32,552 Hz at step 0x800 (12.5 MHz
/ 384), not the game's rates – each effect's step is its rate × 2048 / 32,552, and its samples are resampled to
exactly the rate that step plays, so it sounds at the game's pitch. All 21 fit the chip's 64 KB at about 9 kHz (ring,
jump) and 6.5 kHz (the rest). The ring has a channel of its own; the others share seven. The stages' own effects
(`Data/SoundFX/<zone>`) are not in yet.

## The credits screen

The menu's `CREDITS` row, after MicroNut99's Sonic32XPlus1-RealHardware (`mars/menu.asm` `Mars_Credits`,
`mars/sh2/plasma.c`):

- OpenJazz's plasma on the 32X layer, unaltered (`cd32x/engine/scd_plasma.h`, `oj_sin.h` copied from the template),
  behind the Genesis picture;
- the credits roll up from the bottom of the screen on plane B, a quarter pixel a frame (`G0S254.MDG`);
- the template's keys: **Up / Down** plasma faster / slower, **X** text off / on, **B** or **Z** back to the menu;
  **Left / Right** choose a CD track (2 to 39: the disc's audio tracks, from `engine/CDTracks.inc`), **A** play,
  **C** stop, **Start** pause / go on. The CD line shows the track and the drive's state. With no new boot-stack
  command, pause stops the track and "go on" plays it again from its start, and there is no track count.

## Not in R4A yet

The special stages (the 3D floor), the title, story and ending screens, time attack, animated tiles
(the R0 list: R5xD, R6x, R8x), the underwater colours of Tidal Tempest (the 68000's line interrupt), the stages'
own sound effects, saving.

## Licences

The credits' plasma is **GPL-2.0** code (OpenJazz's `Plasma::draw`, © 2010 Alireza Nejati, through MicroNut99's
template), and its 16 colours are Jazz Jackrabbit's menu palette (`MENU.000`, as in the template). The RSDKv3 decompilation's licence forbids commercial use, a restriction GPL-2.0 does not allow on a
combined work. So a disc or binary that has the credits screen in it should not be published as it stands; for a
public release the plasma would be replaced by an effect of our own, or its author asked. (Not legal advice.) A build
for your own use is not affected.

## Checked before burning

- **sh2emu, every act** (`tools/sh2emu`, its README): the R4A build with all the acts on its disc, each run through
  the Level Select menu as a player would – DEMO PAD on, `STAGE SELECT`, Sonic or Tails, `REGULAR`, the act, Start –
  then 25 seconds of play with the scripted pad: **all 70 acts as Sonic and as Tails (140 runs), `no problems found`
  in every one**. In every one the scene's own native scripts were in use (sh2emu `-peek scdBlob`) and the
  interpreter did not show in the profile. The files of the final build (made from scratch with the commands below)
  are byte for byte the ones the runs used.
- **An act's end:** `START GAME` with DEMO PAD on, 9,000 frames: Palmtree Panic 1 played through by the scripted pad
  (zone complete: track 35), `L1S004.IMG` and `G1S004.MDG` loaded, Palmtree Panic 2 with its native scripts, the
  score carried over (5,900), `no problems found`.
- **Speed** (sh2emu's `game speed`, the play including the act's restarts when the scripted pad loses a life):
  58.3-59.7 frames a second, most acts 59.7; Quartz Quadrant 1 / 2 past 59.4-59.7. Slowest: Collision Chaos 2 past
  as Sonic, 52.4 (many objects; its slowest frames take 1.2-1.3 vertical blanks).
- **Music:** each act starts its own track (Palmtree Panic 1: track 9, Collision Chaos 2 present: 12, Stardust
  Speedway 1 present: 24); game over plays track 34 and then, the title not being on the disc, the menu comes back
  with `NOT ON THE DISC`. **Sound effects:** the bank loads (21 effects); in Palmtree Panic 1 the ring, jump, spin
  dash (charge, release), spring, destroy, rings lost and the future time post play.
- **Credits and menu:** sh2emu's pictures (the VDP model now draws the 32X layer too) show the menu and the credits
  over the plasma; `A` plays track 2; Left on track 2 goes to 39, Right on 39 back to 2.
- **The delivery on its own:** R3A's files + `R4A_files.zip`, built with the commands below (all but the 68000 part,
  which needs the m68k toolchain): every one of the 290 files in `R4A_expected.txt` the same.
- **Quartz Quadrant 1 / 2 past** (PC check): the planes identical to the engine's layers, 1,799 frames each.

## Build

In WSL (the toolchain at `/opt/toolchains/sega`; `ffmpeg` for the music: `sudo apt-get install -y ffmpeg`). The game
data steps write `engine/CDTracks.inc`, `engine/SFXBank.inc`, `engine/ScriptNative.*` and everything in
`game_data/` – made from your `Data.rsdk`, they stay on your computer.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4)
make -C host clean && make -C host
rm -rf game_data                                                          # R3's files (L1S000.IMG was the title) must not reach the disc
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data   # music/, KOBOSFX.BIN, engine/CDTracks.inc, engine/SFXBank.inc
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk                 # engine/ScriptNative.inc + .ops (the global scripts)
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 build_engine/engine.bin                                      # the engine first: the scenes' native scripts are linked against it
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "1:*,1:*@1" --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN cd32x/build_engine/disc/                         # the Makefile does not copy it
make -C cd32x ENGINE=1 ROUND=R4A
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music                                  # -> cd32x/build_engine/SCDENG_AUDIO.bin + .cue: burn this one
```

With the music the disc is about 73 minutes: the data track about 23, the music 49.5 (`build_audio_disc.py` says how
full it is; an 80-minute CD-R holds it, a 74-minute one too). The engine's link prints `RAM end ... (must stay below
0603A400)`: that limit is the Makefile's old text - the engine's RAM may reach 0603D400 (`engine_cart.ld` checks it;
R4A ends at 0603D098, as R3A did).

**Check the build** against mine (the same sources and data give the same bytes):

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
md5sum -c --quiet R4A_expected.txt && echo "R4A_expected: all 290 OK"           # any file that differs is listed
grep -ac "BUILD: SONIC CD R4A GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
```

Then sh2emu (its README, section 2 - R4 builds need a `-pad` to leave the Level Select menu).

## What to look for on the console

1. Boot and loading as R3A (`BUILD: SONIC CD R4A GAME START`, the green loading lines – a little shorter: the scene
   image is half R3A's size), then the **Level Select menu**: white letters on black, the cursor on `START GAME`.
2. `START GAME` (Start or A): Palmtree Panic 1 – its act file loads (loading text, about 15 seconds), the title card,
   **the music**, and Sonic under **your control**: A, B or C jumps, Start pauses. Rings, jumps, spin dash: **sound
   effects**.
3. **A + B + C + Start**: back to the menu.
4. `STAGE SELECT`: `SONIC` or `TAILS`, `REGULAR`, an act, Start. Only the 32 KB chunks of the scene image that
   differ load, then the act file: another zone about 24 of 34 chunks and 12-14 for the act file, the other
   character in the same act 8 and 13 (R3 measured about a second a chunk: up to about 40 seconds). Then the act,
   with its music.
5. An act's end, a time warp: the next scene's files load (the loading text), then it plays – the same way as from
   the menu.
6. `CREDITS`: the plasma, the credits rolling up from the bottom. `A` plays track 2 (Palmtree Panic's past), `C`
   stops, Left / Right choose a track, `B` back to the menu.

| What you see | Meaning |
|---|---|
| the menu again, `NOT ON THE DISC: L1S000.IMG` | the title screen after a game over (or `L0S128.IMG` as Tails): not on this disc |
| the menu again, `NOT ON THE DISC: L3S...` | a special stage (the big ring at an act's end): not on this disc |
| the loading text stays, line 10 `5C61` | the act's `G1S*.MDG` is not on the disc (`build_mdg.sh` missed it): the act runs, no picture |
| no sound effects, the music plays | `KOBOSFX.BIN` is not on the disc (the copy step before `make`) |

## Console log

| Disc | Result |
|---|---|
