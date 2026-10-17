# R4C – the game's own title and menus, the special stages, saves, faster loading, animated tiles

**Goal of the R4C disc:** the whole game on one disc. Power-on goes to **Sonic CD's own title screen** and **menus**
(save select, player select, time attack), the **special stages** play, the game **saves** to the Sega CD's backup
RAM, scenes **load faster**, and the **animated tiles** of Tidal Tempest, Quartz Quadrant, Wacky Workbench and
Metallic Madness move. From your R4B list: the stage select's cursor repeats more slowly, the credits start with the
music, and the track number is at the far left.

**Boot stack:** the 68000 listener (`cd32x/kobo/hw_md.s`) is unchanged. **No new command numbers.** Two things the
Sub-CPU already does got wider:

- **Command 46** (a 32 KB chunk into the cart) takes a **run of up to 4 chunks** when the SH2 sets COMM2's high byte.
  The Sub-CPU reads them in one CD read (one seek instead of four) into its own 128 KB Word RAM bank, copies them into
  the cart, and answers with a checksum. The SH2 checks the cart against it. If the checksum differs, or there is no
  answer, those chunks load again one at a time the old way: the Sub-CPU reads each one back and compares every byte.
  With COMM2's high byte 0, the old path is unchanged since Kobo.
- **The backup RAM file** (Kobo's commands 56-59) is Sonic CD's own now: `SONICCD32X_`, 512 bytes (8 blocks of the
  Sega CD's internal backup RAM; Kobo's was 128 bytes).

The SH2 loader (`cd32x/sh2_engboot.c`, `SCDSH2.BIN`) uses the fast command 46 for the engine and the first scene. It
loads the title screen's image (`L0S000.IMG`) instead of Palmtree Panic 1's.

## Power-on: the game's own front end

1. The SEGA logo, one or two seconds of the white and green text, then **the loading picture** with its bar, as in R4B.
2. **The title screen**: Sonic on the emblem, the clouds, the title music (track 2), PRESS START.
3. **Start: the main menu** – `START GAME`, `TIME ATTACK`, `LEADERBOARDS`, `ACHIEVEMENTS`. Up / Down choose, A (or
   Start) picks.
4. `START GAME`: **save select** – four save files (`NEW GAME` until used; a used one shows its zone), then **player
   select** (Sonic; Tails once the game unlocks him). A: the game starts at Palmtree Panic 1, or where the file left
   off.
5. `TIME ATTACK`: the game's own time attack screen. Its zones stay locked until they are played in the game (the
   game's own rule: every zone shows its padlock on a new backup RAM).

**The Options Menu** (R4B's: player, stage select, lives, demo pad, debug mode, credits) is still there: **A + B + C +
Start** from anywhere – the title, the menus, an act, a special stage. The R4B menu's way back to the title is its
`START GAME` row: it starts Palmtree Panic 1 directly, as before.

The end of the game (the credits after the good or the bad ending) shows this port's credits screen, then the title.

## The 32X picture mode

The title, the menus, time attack and the special stages are not tile layers the Genesis planes can show: they rotate,
scale, blend and draw a 3D floor. In R4C the SH2 draws them itself, into the 32X frame buffer, in 256 colours (the
game's own palette), with the Genesis display off – `cd32x/engine/scd_pm.h`, written for the SH2 from the engine's own
drawing functions:

- **sprites** (flipped, scaled, rotated and zoomed), **rectangles**, **the 3D faces** (the special stages' backdrop and
  shadows), **the tile layers**, **the special stages' floor** (`Draw3DSkyLayer`);
- **translucency** (the menus' glass panels, fades between pictures): a 4 × 4 dither of the two colours where the
  engine mixes them; a whole-screen fade goes on the colours instead (the 32X palette), as on the Genesis planes;
- **two frame buffers**: the SH2 draws a frame into one while the 32X shows the other; they swap in the vertical blank.
  A frame that takes longer than its vertical blank makes the next ones logic only (up to 10 in a row) until the game
  is on time again: the game keeps its speed, the picture comes less often;
- **the scene's image** carries its sprite sheets' and tiles' pixels (`tools/build_game_data.py`: the presentation and
  special stage lists – about 1.6 to 2.3 MB an image), read from the cart.

**Checked on the PC:** every presentation and special-stage scene drawn by `scd_pm.h` beside the engine's own 16-bit
picture, frame by frame (`SCD32X_PMCHECK`, `host/plat_host.cpp`): the same picture, apart from the dithered blends.

**Speed (sh2emu):** the title about 43-48 frames a second, the menus about 41-46. **The special stages are slow:** the
game runs at about 46-48 frames a second (80 % of its speed: the stage's clock runs slower too), and the picture
changes about **4 to 5 times a second**. A picture takes about 11 vertical blanks of the SH2: the floor about 4, the
backdrop's 3D faces about 3, the two background layers about 3. Each sample of the rotating floor is about 25 SH2
instructions and a cart read, for 16,000 samples a picture (already every other line). The way to more pictures a
second is the 32X's second SH2, which is idle: it could draw half of every picture. That needs a small change in the
SH2 loader (its `secondary()` waits for work from the engine instead of a loop; no new Sub-CPU command) – not in R4C
without your say.

## The special stages

All 8, as Sonic and as Tails (`L3S000-007.IMG`, `L3S128-135.IMG`): the big ring at the end of an act with 50 rings
brings the stage, as in the game; the results, the time stones, then the next act. From the Options Menu: `STAGE
SELECT`, `SPECIAL`, the stage.

## Saves

The game keeps all that lasts – four save files, the options, the time attack records – in its save slots 0-173. The
engine packs them into 185 words (`cd32x/engine/scd_save.h`: a header, a checksum, the slots), and the Sub-CPU keeps
them in the backup RAM file `SONICCD32X_` (512 bytes, 8 blocks). The game reads it at the title and the menu, and
writes it when it saves (a new game, the end of an act, a new record); nothing is written when nothing changed.

- **No room in the backup RAM** (8 free blocks needed: the Sega CD's BIOS screen shows them): the game plays without
  saving, every power-on a new backup RAM to it.
- **The file from another version** (its header or checksum wrong): treated as no save; the game's next save writes
  over it.
- While the words go through the COMM registers, the loader's PC sampler is switched off (the independent review
  found that a sample during the stage load could garble one).

## Faster loading

Every load – the boot loader's and the engine's scene changes – reads runs of up to four 32 KB chunks with one CD
command (command 46's run, above). A scene change of about 30 chunks is about 8 CD reads instead of 30, so about 22
fewer seeks. If a run's checksum is wrong, its chunks load again the old, verified way (sh2emu: `-badrun`); a Sub-CPU
program without the run (R4B's) gets the old way for everything (sh2emu: `-oldsub`).

## Animated tiles

The scripts' `Copy16x16Tile` – the conveyor belts, lights and machinery of Tidal Tempest 1, Quartz Quadrant's bad
futures, every Wacky Workbench act and most Metallic Madness acts (22 acts) – moves on the Genesis planes now. The
act file has the cells of every picture the act's animations show (`tools/build_mdg.sh` plays each act for 15 seconds
on the PC first and logs them: they all show within its first four seconds); the SH2 keeps which picture each tile
shows (the same table the picture mode uses) and rewrites the cells of a changed tile that the planes hold
(`md_plane_refresh`, `md/md_planes.c`). The act file's section `ANI1` (`md/sgb.h`). Checked on the PC: the 22 acts
for 90 seconds each with the scripted pad through the record and 68000 model (`SCD32X_SGBTEST`): no pixel different
from the engine's own layers outside the water lines.

## Your R4B list

- **The stage select's cursor** (the Options Menu's `STAGE SELECT`): one row a press; held, it moves on after half a
  second, then 5 rows a second.
- **The credits**: the music plays from the start; the CD line is `TRACK nn` at the far left, no status word.

## Not in R4C

- **Tidal Tempest's underwater colours** (the 68000's line interrupt: a 68000 change).
- **The stages' own sound effects** (the shared ones play): the Sub-CPU's sound effect bank (command 53) holds one
  file, `KOBOSFX.BIN`, at most 31 effects in 64 KB – a stage's own effects need a Sub-CPU change (a bank a stage).
- **The special stages at full speed** (above: the second SH2).
- `LEADERBOARDS` and `ACHIEVEMENTS` are the PC version's online screens: nothing behind them on the console.

## Checked before burning

CHECKS_TBD

## Build

In WSL, as R4B (the toolchain at `/opt/toolchains/sega`, `ffmpeg` for the music). Three differences: the engine is
built with `START="0 0"` (the title screen first – your Makefile's `START ?=` takes it; no Makefile change), the
scene list has the presentation and special stage lists (`0:*` and `3:*`, `3:*@1`) and `--start 0:0`, and
`ROUND=R4C`. **The disc needs an 80-minute CD-R**: about 78.8 minutes (the data track about 28 minutes, the music
50.8) – `build_audio_disc.py` says how full it is.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4C_files.zip && md5sum -c --quiet R4C_md5.txt && echo "R4C_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4C)
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data   # music/, KOBOSFX.BIN, engine/CDTracks.inc, engine/SFXBank.inc
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk                 # engine/ScriptNative.inc + .ops (the global scripts)
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin                          # the engine first (R4C: the title screen first)
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/       # the Makefile copies neither
make -C cd32x ENGINE=1 START="0 0" ROUND=R4C
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music                                  # -> cd32x/build_engine/SCDENG_AUDIO.bin + .cue: burn this one
md5sum -c --quiet R4C_expected.txt && echo "R4C_expected: all 316 OK"
grep -ac "BUILD: SONIC CD R4C GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4C" cd32x/build_engine/disc/APP.BIN                              # 1: the R4C SH2 loader is in
```

Then sh2emu, before burning (its README, section 2): power-on to the title, Start, `START GAME`, the first save file,
Sonic – Palmtree Panic 1:

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 6000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -bram /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin -shot 1000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_title.bmp -shot 2200 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_menu.bmp -shot 3200 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_save.bmp -shot 5800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
```

`VERDICT no problems found`, `backup RAM SONICCD32X_: 3 loads, 1 saves, 256 words exchanged`; the four pictures: the
title screen, the main menu, save select (player select over it), Palmtree Panic 1.

## What to look for on the console

1. **Power-on:** the SEGA logo, one or two seconds of the white and green text, the loading picture with its bar
   (it should fill faster than R4B's: four chunks a CD read), then **the title screen** with its music. (R4B went to the Options Menu.)
2. **Start:** the main menu over its moving background, `START GAME` to `ACHIEVEMENTS`. The menus run at
   about 40-48 frames a second (the 32X draws them: a little slower than the game's own 60).
3. `START GAME`: **save select**, four `NEW GAME` files on a backup RAM without the file; A, then player select, A:
   Palmtree Panic 1 on the Genesis planes as in R4B.
4. **Power off, on again:** save select shows the file you played (`PALMTREE PANIC 1` and its icon) – the save.
5. **A + B + C + Start** on the title or in a menu: the Options Menu (R4B's). `STAGE SELECT`, `SPECIAL`, a stage:
   **the special stage** – the rotating floor, the UFOs, the backdrop. The picture changes about 4-5 times a second
   (above); the game runs at about 80 % of its speed. When the time runs out: the results, then Palmtree Panic 1.
6. **Wacky Workbench 1** (Options Menu, `STAGE SELECT`, `REGULAR`, the 41st act): the lights, the machines and the
   floor's panels move (R4B: frozen). Metallic Madness 1 and Tidal Tempest 1 the same.
7. `TIME ATTACK`: the zone pictures scroll; every zone shows a padlock until it is played in the game.
8. The Options Menu's `STAGE SELECT`: a held Down moves one row, then after half a second 5 rows a second.
   `CREDITS`: the music from the start, `TRACK nn` at the far left.

| What you see | Meaning |
|---|---|
| the Options Menu at power-on, not the title | the engine was built without `START="0 0"` (the build's `make ... build_engine/engine.bin` step) |
| the Options Menu with `NOT ON THE DISC: L0S...` or `L3S...` | the presentation or special stage images are not on the disc (`--scenes "0:*,..."`) |
| a frozen loading bar | a CD read the Sub-CPU never finished (R4B timed out with 5CE2 here): tell me how far the bar got |
| save select always `NEW GAME` after a power cycle | the backup RAM is full (8 free blocks needed) or the write failed: the Sega CD's BIOS screen shows its files |
