# R4B – the loading picture, the Options Menu, 32X skies, the time warp, the pause menu

**Goal of the R4B disc:** R4A's game with your R4A list fixed and added to: a **loading picture** with a bar (at
power-on and between the scenes) instead of the green text, the **Options Menu** (player, lives, debug mode), the
**pause menu** (continue, restart, exit to the menu), **32X skies** behind the levels, the **time warp** working, the
ramps' missing graphics, Palmtree Panic's waterfalls, a louder spin dash.

**Boot stack:** the Sub-CPU program and the 68000 listener are unchanged (APP.BIN differs only by the round's label
and the SH2 loader inside it). **The SH2 loader (`cd32x/sh2_engboot.c`, SCDSH2.BIN) changes for the first time since
R1S**: it shows the loading picture, the way OpenLara's, OpenJazz's and Hurrican's loaders show theirs. No new
command: it uses the Sub-CPU's commands 51 (a file into the frame buffer) and 52 (the Genesis display on / off), both
in the Sub-CPU program since Kobo.

## The loading picture

The title screen – Sonic on the emblem, the clouds, Little Planet – with a loading bar where PRESS START shows
(`IMAGE.RAW`, made from your `Data.rsdk`: the PC engine plays the title screen and the frame is taken with PRESS
START blinked off; `tools/scd_splash.py`).

- **At power-on** the boot loader opens `IMAGE.RAW` (file 0), has the Sub-CPU put it into the frame buffer (command
  51), shows it on the 32X layer and switches the Genesis display off (command 52): the white and green text goes
  away. The bar fills while the engine, the data pack and the first scene load (about 50 chunks). The Options Menu
  replaces it. The first one or two seconds of text come from the Sub-CPU before the SH2 starts: they still show.
- **Between the scenes** the engine does the same with its copy of the picture (at the end of `SCDDATA.PAK`, in the
  cart since power-on): the bar fills while the scene's chunks load (the first 20 of its 32 segments) and while the
  act file loads (the rest). Then the act's planes come on.
- **The bar** does not draw: its 32 segments are 32 colours of their own, set to yellow one by one.
- **A restart of the same act** (a lost life, RESTART in the pause menu): nothing is read from the disc any more –
  the scene and its act file are in the cart already – and the screen stays black until the act comes back. R4A
  read the act file again (12-14 chunks).
- **If something does not load** (an act file not on the disc, a bad chunk) the picture goes and the Genesis text
  comes back, so line 10 shows the STATE code as before. Without `IMAGE.RAW` on the disc the boot shows the text as
  in R4A; with an older `SCDDATA.PAK` (no picture in it) the scene changes show the text as in R4A.

## The Options Menu

| Row | What it does |
|---|---|
| `PLAYER` | Sonic or Tails (A, Start, Left or Right switches) – for both rows below |
| `START GAME` | Palmtree Panic 1 |
| `STAGE SELECT` | the stage list (`REGULAR` first), then the act; Start or A plays it |
| `LIVES` | 1 to 9: the lives a game starts with (Right / A / Start one more, Left one less) |
| `DEMO PAD` (off) | the scripted pad of R1-R3 |
| `DEBUG MODE` (off) | every stage in the game's debug mode – the only way to it now (R4A: A on the act did it too, the original's way – on this pad, where A also chooses, it came on by accident: your Tails video) |
| `CREDITS` | the credits screen |

The highlighted row is yellow now. A game from the menu starts as a new game does: score 0, the next extra life at
50,000, no time stones, the menu's lives.

## The pause menu

Start in an act pauses (every time – R4A only once): `CONTINUE`, `RESTART`, `EXIT` with the game's own sprites, over
the level dimmed (the planes' colours at about 40 %). `EXIT` goes to the Options Menu. A + B + C + Start still does too.

## The rest

- **32X skies** in 27 acts: every Palmtree Panic and Collision Chaos act, Quartz Quadrant 3's bad future, Wacky
  Workbench 1 and 2 past, Metallic Madness 1 and 2 past and 2 and 3 bad future. Where an act's background shows its
  sky colour, the Genesis tiles let the 32X layer through, and the 32X shows a gradient there, made for each act from
  its own colours (the background's rows above and below the sky), moving with the background, following the fades
  and the pause menu's dimming. Each act file carries it (`SKY1`, `md/sgb.h`). Stardust Speedway has none (your
  call: its night skies look better as drawn); the other acts have no open sky colour in their backgrounds.
- **The time warp.** The scripts are made for 240 lines: the warp's Sonic starts at y 480, below what the port's
  object border (224 + 256) let run, so the warp never ended. The border is the scripts' (240 + 256) now.
- **Ramps and loops running backwards** (Palmtree Panic 2, your photo): the tile triggers change the layout as Sonic
  runs through them; the act file now has the cells of every chunk the scripts can place (`tools/scd_chunks.py`), and
  the SH2 rewrites a changed chunk on the planes (`md_plane_chunk`).
- **Palmtree Panic's waterfalls** move again (the engine's palette banks).
- **The spin dash** (and the other quiet effects) at full level on the PCM chip.

## Not in R4B yet

- The game's own title, opening and ending screens and menus (the Options Menu stands in; a game over, the end of the
  game or `EXIT` brings it back).
- Saving – decoded: four save files, the options and the time attack records. They go to the Sega CD's backup RAM in
  a file of their own (`SONICCD32X_`, 512 bytes: a small Sub-CPU change, no new command), with the title and menu.
- Time attack; the special stages (the big ring brings the menu back with `NOT ON THE DISC`).
- The animated tiles of Quartz Quadrant's good futures, Wacky Workbench and Metallic Madness.
- Tidal Tempest's underwater colours (the 68000's line interrupt: a 68000 change).
- The stages' own sound effects (the shared ones play on the PCM chip).
- The first one or two seconds of text at power-on (the Sub-CPU prints them before the SH2 starts).

## Checked before burning

- **sh2emu** (its README; it now plays the Sub-CPU's commands 51 and 52 too, and `-pokeg` sets a game variable at a
  frame), on the build made from scratch with the commands below:
  - **Power-on:** `IMAGE.RAW` opened at frame 2, the Genesis display off, the bar full when the first scene is in;
    the Options Menu at frame 301 (sh2emu's loading is faster than the console's: about a second a chunk there).
  - **The README's check** (`START GAME` with the scripted pad, 5,000 frames): `no problems found`, 59.6 frames a
    second, the Genesis display on again at the end.
  - **Every act as Sonic and as Tails** through the Options Menu, 25 seconds of play each: **all 70 acts as Sonic and as Tails (140 runs): `no problems found` in every one**, each with the scene's native scripts in use (`-peek scdBlob`) and the Genesis display on at the end; the 7 Stardust Speedway acts again with their act files without the sky: the same
  - **A scene change** (Palmtree Panic 2 from the menu): 21 scene chunks with the bar's first part, the act file with
    the rest, then the act. **A restart** (the pause menu's `RESTART`): black, no disc read, the act back.
  - **The time warp** (Palmtree Panic 1, `Warp.Destination` set to the future while the scripted pad ran): the light
    blue flash, the loading picture, the future act (`L1S003.IMG`, `G1S003.MDG`), played on.
  - **The ramps:** Palmtree Panic 2 right, then left: the tile triggers changed four layout entries each way, the SH2
    rewrote those chunks on the planes, the loops complete in the pictures.
  - **The pause menu's `EXIT`:** the Options Menu. **`LIVES` 9:** `x9` in the HUD. **`CREDITS`:** the plasma and the
    roll, `B` back to the menu.
  - **Speed:** median 59.5 frames a second over the 140 runs, 56-60 in all but one. Collision Chaos 2 past as Sonic, the slowest in R4A too: 49 frames a second (R4A: 52)
    – its frames take 1.00 vertical blanks on average (R4A 0.94), so about half miss the vertical blank. The sky's
    colours take under 1 % (they are written only when the background moves up or down, or a fade runs); most of the
    rest is the game's own object work (touch and floor checks, the objects' scripts), which grew with the object
    border's 16 more lines – what the time warp needed.
- **An independent review** of the loader and engine changes (a separate agent, against the Sub-CPU's commands and
  OpenLara's loader): nothing that stops the boot; three small things, fixed – the menu's failure path left the
  Genesis display off, the credits wrote their 16 colours while the picture was shown (now in blank), the loader
  waited for a vertical blank after every chunk (now only when a segment fills).
- **The delivery on its own:** R4A's files + `R4B_files.zip` + your Makefiles, built with the commands below (all but
  the 68000 part, which needs the m68k toolchain): every one of the 291 files in `R4B_expected.txt` the same – the scene images, the act files, `SCDDATA.PAK`, `IMAGE.RAW`, `KOBOSFX.BIN`, the engine (`SONICCD.PAK`), the loader (`SCDSH2.BIN`) and the engine's generated includes.

## Build

In WSL, as R4A (the toolchain at `/opt/toolchains/sega`, `ffmpeg` for the music). Two differences: `IMAGE.RAW` is
copied into the disc folder next to `KOBOSFX.BIN` (the Makefile copies neither), and `ROUND=R4B`.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4B_files.zip && md5sum -c --quiet R4B_md5.txt && echo "R4B_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4B)
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data   # music/, KOBOSFX.BIN, engine/CDTracks.inc, engine/SFXBank.inc
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk                 # engine/ScriptNative.inc + .ops (the global scripts)
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 build_engine/engine.bin                                      # the engine first: the scenes' native scripts are linked against it
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "1:*,1:*@1" --native cd32x/build_engine   # + IMAGE.RAW (R4B)
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/       # the Makefile copies neither
make -C cd32x ENGINE=1 ROUND=R4B
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music                                  # -> cd32x/build_engine/SCDENG_AUDIO.bin + .cue: burn this one
md5sum -c --quiet R4B_expected.txt && echo "R4B_expected: all 291 OK"
grep -ac "BUILD: SONIC CD R4B GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4B" cd32x/build_engine/disc/APP.BIN                              # 1: the new SH2 loader is in
```

Then sh2emu, before burning (its README, section 2) – the R4B menu: `START GAME` is where the cursor starts, `DEMO PAD`
three rows down:

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 5000 -pad "600:D,610:-,620:D,630:-,640:D,650:-,660:A,670:-,680:U,690:-,700:U,710:-,720:U,730:-,740:S,750:-" -shot 200 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_loading.bmp -shot 590 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_menu.bmp -shot 1500 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
```

`VERDICT no problems found`, `Genesis display on at the end; the loading picture: command 51 used`; the three
pictures: the loading picture with its bar, the Options Menu, Palmtree Panic 1 with its sky.

## What to look for on the console

1. **Power-on:** the SEGA logo, one or two seconds of the white and green text (the Sub-CPU and the 32X waking up –
   the Sub-CPU's line 4 now ends `SH2-VR4`), then the **title screen picture** with a yellow bar under the logo,
   filling while the engine, the data pack and the first scene load (about as long as R4A's loading text). Then the
   **Options Menu**: the cursor (yellow) on `START GAME`.
2. `START GAME`: the picture again with an empty bar – the bar's first part fills at once (Palmtree Panic 1 is in
   the cart since power-on), the rest while the act file loads – then the act, **with a 32X sky**: a gradient behind
   the clouds, from the act's own blues.
3. **Start**: the pause menu – `CONTINUE`, `RESTART`, `EXIT`, the level dimmed. Again later: it opens every time.
   `RESTART`: a short black screen, the act from its start (no loading picture: nothing is read from the disc).
   `EXIT`: the Options Menu.
4. `PLAYER`: `TAILS`, then `STAGE SELECT`, `REGULAR`, an act: Tails. `LIVES` 9, `START GAME`: 9 in the HUD.
5. **A time post and running fast** (Palmtree Panic 1's future post): the warp – the light blue flash – then the
   loading picture and the future act. R4A stayed on the light blue screen.
6. **Palmtree Panic 2, running back** through the loops and ramps after passing them: no missing pieces.
7. Palmtree Panic's **waterfalls** move; the **spin dash** is heard over the music.

| What you see | Meaning |
|---|---|
| the green and white text all the way to the menu, no picture | `IMAGE.RAW` is not on the disc (the copy step before `make`) – the rest works as in R4A |
| the green loading text between the scenes (no picture) | the `SCDDATA.PAK` on the disc has no picture in it (an older one) |
| the picture goes, the text comes back with line 10 `5C61` | the act's `G1S*.MDG` is not on the disc (the act runs, no picture - as R4A) |
| the menu again, no line under `SEGA CD32X` | a game over or `EXIT`: the title screen, which the Options Menu stands in for |
| the menu again, `NOT ON THE DISC: L3S...` | a special stage (the big ring at an act's end): not on this disc yet |
