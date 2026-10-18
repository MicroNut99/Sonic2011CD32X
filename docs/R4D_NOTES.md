# R4D – the second SH2, each stage's own sound effects, the underwater colours

**Goal of the R4D disc:** three changes to the boot stack on one disc, each behind its own switch in the Options Menu.
**All three are off at power-on: with them off the disc plays as R4C** (the same sounds, loads and Genesis
pictures), plus the fixes from your R4C video below (the title's demos, faster menus, short presses in a menu).

| Switch | What it does | Boot stack part |
|---|---|---|
| `2ND SH2` | the 32X's second SH2 draws half of every picture of the 32X picture mode (the special stages, the title, the menus) | the SH2 loader (`cd32x/sh2_engboot.c`, `SCDSH2.BIN`): its `secondary()` waits for work from the engine |
| `STAGE SFX` | every scene loads its own sound effects into the PCM chip (the stage's own: bumpers, flippers, the drowning alarm ...) | the Sub-CPU (`cd32x/sub_main.c`): command 53 takes a bank number |
| `UNDERWATER` | the water colours below the water line (Tidal Tempest): the Genesis planes and sprites | the 68000 listener (`cd32x/kobo/hw_md.s`): a line interrupt (H-int) at the water line |

**No new command numbers.** Command 53 (load the sound effects) reads COMM2 as the bank: 0 is `KOBOSFX.BIN`, as in
R4C (R4C's engine always sent 0).

## The Options Menu

A + B + C + Start, as before. Three rows before `CREDITS`, each `OFF` at power-on; A, Start, Left or Right switch one:

```
PLAYER     SONIC
START GAME
STAGE SELECT
LIVES      3
DEMO PAD   OFF
DEBUG MODE OFF
2ND SH2    OFF
STAGE SFX  OFF
UNDERWATER OFF
CREDITS
```

The menu starts higher on the screen (the two blank rows under `OPTIONS MENU` and the version are gone) so the 26 rows
fit. A switch stays as set until the power goes off; `START GAME` or `STAGE SELECT` then plays with it.

- `2ND SH2` works from the next picture.
- `STAGE SFX` works from the next scene the game loads (the scene's bank is loaded with it).
- `UNDERWATER` works from the next act the game loads (the act's first frame sends the 68000 all its colours).

## Fixes from the R4C disc (your video, 17 Oct)

**On the console (R4C):** power-on to the title, the main menu, save select and player select; a save in the
backup RAM, and after a power cycle save select shows it.

- **The title's demos ended after a second or two.** Each demo plays an act with recorded buttons
  (`Data/Game/Demos/Key11a.bin`, `Key43c.bin`, `Key82a.bin`, `Special1.bin`, `Special6.bin`). The acts' data packs are
  made from PC runs that start each act directly, so those files were in no pack: on the console the demo found no
  buttons and went back to the title. Each one is in its act's pack now (`tools/build_stage_packs.py`). Reproduced in
  sh2emu with the R4C build: Palmtree Panic 1's demo went back to the title after 3.5 seconds (the game plays it about
  33).
- **A game started from the Options Menu is never a demo:** `START GAME` and `STAGE SELECT` set the game's demo flag
  (`Options.AttractMode`) to 0, as they set the lives and the score. (A guard: your report - an act from `STAGE
  SELECT`, Sonic played a moment, then its title card again - was a lost life in sh2emu: Sonic with no rings touched
  the first badnik, the lives went 3 → 2 and the act started again from its title card, as the game does.)
- **The menus were slow** (about 3.6 pictures a second in sh2emu). Half of the main menu's drawing is its moving
  background: two pictures over the whole screen, each half see-through. R4C drew them pixel by pixel through the
  general dither; a half see-through picture is a plain checkerboard (one pixel of each pair), so it goes one byte
  read and one word written per pair now - the same pixels. With `2ND SH2` on, the second SH2 draws half of every big
  picture too. Main menu, sh2emu: @@MENUS@@.
- **The dither's pattern slipped** after a see-through pixel of a sprite (R4C moved it on at the visible pixels
  only): every pixel moves it on now, so the pattern stays fixed on the screen.
- **A short press in a slow menu could be lost.** The game reads the pad once a frame; while a picture takes several
  vertical blanks, a press shorter than that came and went unseen (sh2emu: a 10-frame A press in the main menu was
  missed). The pad is asked for once a vertical blank while a picture is drawn now, and a press seen in between counts
  in the next frame.

## Tails from the start (your request, 17 Oct)

Player select (after save select) offers **Tails from power-on** (R4C: his silhouette and a padlock until the game is
finished). The game keeps that in its save slot 36 (its ending sets it; the main menu's script reads it into
`Options.TailsUnlocked`); the engine sets it each time it reads the backup RAM (`engine/Userdata.cpp`), and the next
save writes it into the file. Tails plays the acts and special stages with his own scene images, as from the Options
Menu's `PLAYER` row since R3. On the PC: player select with Tails in colour, Right, A - Palmtree Panic 1 as Tails.

## 2ND SH2

The 32X has two SH2s; until R4D the second one did nothing (the loader's `secondary()` was a loop to itself). Now it
waits for work in a mailbox after the boot record (SDRAM `0x06001F40`, cache-through): the engine writes the job's
function and argument there, the second SH2 runs it and writes back the job's number. Each big drawing of the picture
mode is split by rows - the second SH2 draws every other row, the first SH2 the rest at the same time - and the
first SH2 waits for the other half before it goes on: the special stages' floor, the tile layers, the 3D faces of 24
rows or more, and sprites of 24 rows and 4,096 pixels or more (the menus' pictures). Smaller ones stay on the first SH2 (handing them
over costs more than it saves). The picture is the same either way (checked on the PC: the halves drawn one after the
other give the same picture as the whole).

- **Safe if it does not answer:** the first job is a test (when the switch is first on); a job not done within 30
  vertical blanks is drawn by the first SH2 itself, and the second SH2 is left alone until the power goes off.
- **Off, it costs nothing:** the second SH2 waits in its registers (two reads of the mailbox every 1,000 cycles or
  so: R4C's loop did not use the bus either), so the first SH2 runs at R4C's speed.
- The acts (the Genesis planes) do not use it: their drawing is the 68000's and the VDP's.

## STAGE SFX

R4C's bank (`KOBOSFX.BIN`, 64 KB of PCM wave RAM) has the 23 effects every act shares; the stages' own (bumpers,
flippers, the drowning alarm, the UFOs ...) did not fit. With `STAGE SFX` on, every scene loads its own bank with its
scene: the shared effects of its kind (the acts' 21 + the big ring's two; the special stages' 7; the menus' two) and
the effects of its `StageConfig`, at most 63 (`tools/build_audio.py`: 23 banks, `SFX01.BIN` - `SFX23.BIN`; scenes
with the same effects share one). The longest shared effects are cut a little shorter in the acts' banks to make
room. The rates: the acts' banks 4.2 - 5.7 kHz (24 - 35 effects each), the special stages' 7.0 kHz (15), the
title's and the menus' 8 kHz (`KOBOSFX.BIN`: 5.8 kHz, the ring and the jump 8 kHz) - more effects, a little duller.
Two banks are full: Wacky Workbench 3's leaves out `Crumble`, the sound test's (`Secrets`, 41 effects) three of its
own.

- **The Sub-CPU:** command 53 (load the bank) with the bank number in COMM2: 0 `KOBOSFX.BIN` (R4C's engine always
  sent 0), n `SFXnn.BIN`. A file it cannot read, or that is not a bank, leaves the chip as it was: the scene plays the
  bank before it (status 5C1A).
- A bank loads during the scene change (one CD read of at most 64 KB); off, `KOBOSFX.BIN` stays in the chip (loaded
  once at power-on, as R4C).

## UNDERWATER

Tidal Tempest (and every act with water) has a second palette under the water line. With `UNDERWATER` on, the act's
record each frame (the SH2's list for the 68000) ends with the water line and the colours that differ under it. The
68000 listener (`cd32x/kobo/hw_md.s`) keeps both palettes: in the vertical blank it writes the normal colours and sets
the VDP's line interrupt (register 10) at the water line; the interrupt (the BIOS's H-int entry, `0xFFFD0C`) writes
the 64 water colours into the colour RAM there. The next vertical blank puts the normal colours back. No water line
on the screen (above the screen, below it, or an act without water): the line interrupt is off, as in R4C.

- The interrupt takes about 1,160 68000 cycles (the 64 colours): the change spreads over two or three lines at the
  water line, and colour RAM writes during a line can show as dots there (Sonic 1 changes its water colours the same
  way).
- A line interrupt left over from before (in the vertical blank, or above the water line) does nothing: the
  interrupt checks the VDP's line counter first.
- On, the colours above the line are the screen's first line's. Off, as R4C: the last line's - when the water
  reaches the bottom of the screen, the whole picture shows the water colours (an R4C fault the switch fixes).

@@CHECKS@@

## Build

In WSL, as R4C. Two differences: the stage banks go on the disc with the other sound file (`SFX*.BIN` in the copy
step) and `ROUND=R4D`. The link's "RAM end ... (must stay below 0603A400)" is the Makefile's old text, as before
(`engine_cart.ld` checks the real limit, 0603D400). **The disc needs an 80-minute CD-R**, as R4C: @@DISC@@.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4D_files.zip && md5sum -c --quiet R4D_md5.txt && echo "R4D_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4D)
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data   # music/, KOBOSFX.BIN, SFX01-23.BIN, engine/CDTracks.inc, engine/SFXBank.inc
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk                 # engine/ScriptNative.inc + .ops (the global scripts)
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin                          # the engine first (the title screen first)
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/SFX*.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/   # the Makefile copies none of them
make -C cd32x ENGINE=1 START="0 0" ROUND=R4D
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music                                  # -> cd32x/build_engine/SCDENG_AUDIO.bin + .cue: burn this one
md5sum -c --quiet R4D_expected.txt && echo "R4D_expected: all @@NEXP@@ OK"
ls cd32x/build_engine/disc/SFX*.BIN | wc -l                                      # 23: the stage banks are on the disc
grep -ac "BUILD: SONIC CD R4D GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4D" cd32x/build_engine/disc/APP.BIN                              # 1: the R4D SH2 loader is in
```

Then sh2emu, before burning (its README, section 2) - the same run as R4C (all three switches off): power-on to the
title, Start, `START GAME`, the first save file, Sonic - Palmtree Panic 1. Then the main menu with `2ND SH2` on (`-set`
switches it on in the emulated RAM, as the Options Menu does):

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 6000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -bram /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin -shot 1000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_title.bmp -shot 2200 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_menu.bmp -shot 3200 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_save.bmp -shot 5800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 3600 -pad "1400:S,1410:-" -set scdSh2Switch=1 -shot 3500 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_menu_sh2.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_sh2.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_sh2.txt
```

The first run: `VERDICT no problems found`, `backup RAM SONICCD32X_: 3 loads, 1 saves, 256 words exchanged`, `second
SH2 0 jobs` (the switch is off), `audio ... 1 sound banks loaded`; the four pictures: the title screen, the main menu,
player select with Tails in colour, Palmtree Panic 1. The second: `VERDICT no problems found`, `second SH2` with a few
thousand jobs, `pictures ... in the last 10 s` about @@MENUON@@ a second (the main menu; with the switch off @@MENUOFF@@, R4C 3.6).

