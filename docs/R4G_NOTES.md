# R4G – the preload (the 16 MB cart)

**Goal of the R4G disc:** with MicroNut99's 16 MB cart (battery-backed: what is in it stays through power-off), the
act you play is in the cart **with every time period and the next act's start**, so warps and the next act need no
CD, and after a power-off the game is **ready to play** - unless you continue a save in another act. On the 4 MB
cart the game is R4D's, plus two writes to the page register when the title starts (below). Checked in sh2emu only
(its model of the mapper, R4E, and now of the battery).

R4F's "pairs" are gone: they held 4 scenes, and the boot loader's title took the first one at every power-on.

## How the cart is used

| Pages | Window | Holds |
|---|---|---|
| 0 and 1 | 0 and 1 | as the 4 MB game: the engine, the boot loader's title, the menus, the special stages, the title's demos, the data pack with the loading picture (0x300000), one act file slot (0x380000) |
| 2-6 | 0 | the **act pages**: each its own copy of the engine (the same as the running one, byte for byte) and one act scene's image at 0x080000, used where it lies |
| 7 | 1 | the act files of act pages 2-5, a 512 KB slot each; page 6's goes into page 1's slot |

- **A scene in an act page:** window 0 to its page, window 1 to its act file's page - two page switches, no disc.
- **An act's start** (its first scene - from a save, the menu, or the end of the act before; not a title demo): the
  act's other time periods and **the next act's start** are loaded into the other act pages, with the loading
  picture and its bar, the music off (a CD data read stops it anyway). After act 1: act 2's present; after act 2:
  act 3's bad future (the usual one - the good future loads at its start, as today); after act 3: the next zone's
  act 1. Then the act starts as always. The order, the most needed first: the present, the past, the next act's
  start, the bad future, the good future. The last one goes into page 6, whose act file shares page 1's slot with the
  title demos, the credits and the dev menu's font: after those, only that period's act file is read again (about
  400 KB: near 3 s at 150 KB/s, computed), never its image.
- **Warps and restarts** inside the act: page switches only.
- **After a power-on** the game knows nothing about the pages: the first act scene looks each act page and act file
  slot up in the **preload table** (`tools/build_preload.py`: in `SCDDATA.PAK`, which the boot loader brings in at
  every power-on) by its header's fingerprint, and checks it whole with the loader's checksum. A page that was half
  loaded when the power went off does not match and is loaded again. A few seconds of checking, no disc.
- **Title demos** play in page 0, as on the 4 MB cart: they never push your act out.
- **8 MB cart:** one act page (2), its act file in page 3: it holds the act's first scene (the one you start or
  continue in), which stays there - through a power-off too; the act's other periods load in page 0 as on the 4 MB
  cart, so a warp back to that first scene is a page switch.
- **4 MB cart, or `BIG CART` off:** none of this - the game is R4E's (= R4D's).

The 16 MB cart is found when the title starts (no cart RAM written - only the window 1 page register: to page 0,
then to page 4, each compared with window 0, back to page 1). **On the 4 MB cart these are two writes R4D never made:** to the page register `0xA130F2` (the even byte
in the cart's `/TIME` area, `0xA13000-0xA130FF`; window 1 to page 0, back to page 1), as R4E's `CART TEST` did - but
now at every power-on, not only when you choose the test. A cart that does not use `/TIME` ignores them. Whether your
4 MB cart (v01) uses `/TIME` for anything could not be checked here: the 16 MB repo has only v01's edge connector,
where the `/TIME` pad (B31) is a named net, not its board. You designed it - if v01 does use `/TIME`, keep R4D on the
4 MB cart.

## The Options Menu

The last row reads `BIG CART   16 MB` (or `8 MB`) when the cart was found, `OFF` otherwise. A turns it off (the 4 MB
way); A again runs R4E's cart test and, if it passes, turns it on. The test writes into pages 2-7, so the act pages
are looked up again at the next act.

The test does not run while page 0 holds an act scene (`BIG CART` was off and you played an act, or a title demo is
playing): an act page holding the same scene would look like page 0 itself. The note rows then read `CART TEST: AN ACT
IS IN PAGE 0` / `RUN IT FROM THE TITLE - NOTHING DONE`, and the row stays `OFF`: open the menu from the title screen
and try again.

## The boot stack

**Changed against R4D (your last disc that works): `cd32x/sub_main.c`, the Sub-CPU's program - 23 lines added, two
cases inside the existing command 46** (no new command number; R4E added the first, R4F the second, R4G adds nothing
more):

- COMM2 `0xF8nn` (`0xF800 | window << 8 | page`): writes a page register - the page into Word RAM, then the 68000's
  existing command 26 (`SAFE_CART_XFER`: interrupts off, RV = 1) copies that byte to `0xA130F0` (window 0) or
  `0xA130F2` (window 1). Reply COMM12 = 0.
- COMM2 `0xF0nn` (`0xF000 | slot`, 0-31): the file command 38 opened goes to 128 KB slot nn of the cart instead of
  its own (`cur_img = slot`) until the next command 38. Reply COMM12 = 0.

Both cases are checked before the chunk runs; a chunk run's COMM2 is never `0xF0nn` or `0xF8nn`, so R4D's commands
behave as before. **Why they are needed:** only the 68000 can write the page registers, and only when the Sub-CPU
asks it to; none of R4D's commands lets the SH2 choose where the 68000 writes (every cart write goes into the first
4 MB). Without the first case the game can never see past 4 MB. The second puts the engine's copy and an act scene's
image at a page's own `0x000000` / `0x080000` through window 1 (window 0 cannot point at a page that does not hold
the engine yet).

Unchanged since R4D: the 68000's `hw_md.s`, the SH2 boot loader (`SCDSH2.BIN`, `SH2-VR4D`, md5 below), the power-on
(the engine, the data pack and the title from the disc). Neither the R4E nor the R4F disc was burned: **R4G's disc
is the first to run the new `sub_main.c` on the console.**

## Checked before burning

- **sh2emu**, on the build made from scratch with the commands below (its model of the mapper `-cart 4 / 8 / 16`, of
  the battery `-cartkeep`): `VERDICT no problems found` in every run, no `STALE-READ`, no `CART-W0`.

| Run | Result |
|---|---|
| 4 MB: power-on to Palmtree Panic 1 through the game's menus | the same pictures as R4E's and R4F's runs (record CRC `2F19502C`), the same loads but one more 32 KB chunk of `SCDDATA.PAK` (the table), 1 save; 2 page commands at the title, nothing else |
| 4 MB: the cart test | `CART TEST: NO PAGE MAPPER` / `(A 4 MB CART) - NOTHING WRITTEN`; the row stays `OFF` |
| 16 MB, empty cart: Palmtree Panic 1 present, past, act 2 present (`STAGE SELECT`) | the act start loads the present into page 2 and preloads past, act 2's present, bad future, good future (pages 3-6); the past: a page switch, no file; act 2's present: a page switch, then its own preload (past, act 3's bad future, bad future, good future) |
| 16 MB, power on again with that cart: act 2 present, past, good future | the pages looked up (about 2.5 s), all three scenes from the cart, no image read; the good future's act file read again (its slot is shared with the menu's font) |
| 16 MB through the game's own menus (title, `START GAME`, a save) | the same preload at Palmtree Panic 1's start |
| 16 MB: the power cut 12 frames into the good future's preload, then on again | the half-loaded page found and loaded again in full; the others from the cart |
| 16 MB: real time warps (the game's own, `DEMO PAD`): present → past → present → bad future → present | four page switches, **no file opened**; the right period each time |
| 8 MB: real time warps, the same | the present stays in page 2: back to it is a page switch; the past and the bad future load in page 0 as on the 4 MB cart |
| 8 MB: present, past, present (`STAGE SELECT`) | the same: the second present without a file |
| 16 MB: the cart test after a preload | `CART 16 MB: PAGES 2-7 OK` / `PAGE SWITCH OK, GAME PAGES UNTOUCHED`; the next act start reloads the engine copies it overwrote |
| 16 MB: `BIG CART` off, an act played (in page 0), then the test | `CART TEST: AN ACT IS IN PAGE 0` / `RUN IT FROM THE TITLE - NOTHING DONE`; the row stays `OFF` |
| 16 MB: the title left alone (title demos) | the demos play in page 0, nothing preloaded |
| 16 MB: a title demo, then a game | the game's act start preloads as usual |
| 16 MB, Tails | Tails's scenes (the same order) |

- **The delivery checked on its own:** R4D's tree (your folder before R4E) + `R4G_files.zip` + your Makefiles, built
  from scratch with the commands below: `R4G_md5: all OK`, `R4G_expected: all 340 OK`, `SH2-VR4D` in the loader.
- **A review by another model (Fable)** read the whole repository. It found that the cart test could take an act
  page for page 0 when page 0 holds the same act (now refused, above), and that loads into an act page or slot had no
  size guard (added; no file today is too big). It raised the 4 MB cart's two page register writes (above). It found
  the page bookkeeping, the cache purges, the table's format and the boot-stack additions correct.
- **Not checked:** anything on the console - the new `sub_main.c` cases, the 4 MB cart with the page register writes,
  the 16 MB cart (not built), the CD's real speed (sh2emu reads faster).

## Build

In WSL. `R4G_files.zip` holds every file changed since R4D (R4E's and R4F's too: your folder is at R4D - `R4E_files.zip`
and `R4F_files.zip` need not be unpacked). Against R4F's commands: one more step after `build_mdg.sh` (the preload
table into `SCDDATA.PAK`), `ROUND=R4G`, and the disc image under its own name in `discs/`. The disc needs an 80-minute
CD-R, as R4D.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4G_files.zip && md5sum -c --quiet R4G_md5.txt && echo "R4G_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4G)
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
python3 tools/build_preload.py game_data
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/SFX*.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/ && python3 tools/make_cart_test.py cd32x/build_engine/disc/KOBOGFX.BIN
make -C cd32x ENGINE=1 START="0 0" ROUND=R4G
mkdir -p /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4G
md5sum -c --quiet R4G_expected.txt && echo "R4G_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4G GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4D" cd32x/build_engine/disc/APP.BIN                              # 1: the SH2 loader (unchanged since R4D)
```

`python3 tools/build_preload.py game_data` prints `SCDDATA.PAK: the preload table, 280 entries (140 images, 140 act
files)`. Burn `discs/SONICCD_R4G.cue`.

Then sh2emu, before burning - three runs: the power-on on the 4 MB cart (as R4D), the 16 MB cart's first power-on
(empty cart: Palmtree Panic 1 present with its preload, the past, act 2's present), and a second power-on with the
cart's contents kept (the battery: act 2's present, past and good future from the cart). The cart's 16 MB are kept in
`sh2emu_cart16.bin` between runs 2 and 3.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 6000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -bram /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin -shot 5800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 8500 -pad "1300:ABCS,1310:-,1450:D,1460:-,1480:A,1490:-,1510:A,1520:-,1540:S,1550:-,3700:ABCS,3710:-,3850:D,3860:-,3880:A,3890:-,3910:A,3920:-,3940:D,3950:-,3970:S,3980:-,6100:ABCS,6110:-,6250:D,6260:-,6280:A,6290:-,6310:A,6320:-,6340:D,6350:-,6370:D,6380:-,6400:D,6410:-,6430:D,6440:-,6460:S,6470:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 3670 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_g1.bmp -shot 6070 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_g2.bmp -shot 8470 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_g3.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_g1.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_g1.txt
grep -E "window 0|open L1S" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_g1.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 4900 -pad "1300:ABCS,1310:-,1450:D,1460:-,1480:A,1490:-,1510:A,1520:-,1540:D,1550:-,1570:D,1580:-,1600:D,1610:-,1630:D,1640:-,1660:S,1670:-,2500:ABCS,2510:-,2650:D,2660:-,2680:A,2690:-,2710:A,2720:-,2740:D,2750:-,2770:D,2780:-,2800:D,2810:-,2830:D,2840:-,2860:D,2870:-,2890:S,2900:-,3700:ABCS,3710:-,3850:D,3860:-,3880:A,3890:-,3910:A,3920:-,3940:D,3950:-,3970:D,3980:-,4000:D,4010:-,4030:D,4040:-,4060:D,4070:-,4090:D,4100:-,4120:S,4130:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2470 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_g4.bmp -shot 3670 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_g5.bmp -shot 4870 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_g6.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_g2.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_g2.txt
grep -E "window 0|open L1S|open G1S" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_g2.txt
```

What they print (checked here on the build made from scratch with these commands):

- **Run 1 (4 MB):** `VERDICT no problems found`; `cart 4 MB (no mapper), 2 page switches; window 0 page 0, window 1
  page 1 at the end`; `loading 39 fast runs (153 chunks ...), 10 chunks one by one`; `backup RAM SONICCD32X_: 3 loads,
  1 saves`. The picture: Palmtree Panic 1. (R4F's run read 9 chunks one by one: `SCDDATA.PAK` is one 32 KB chunk
  longer with the preload table.)
- **Run 2 (16 MB, first power-on):** `VERDICT no problems found`; `cart 16 MB (page mapper), 38 page switches; window 0
  page 4, window 1 page 7 at the end`. The grep: `window 0 -> page 2`, then `open L1S000`, `L1S001`, `L1S004`,
  `L1S003`, `L1S002` (the act start and its preload, in that order); `window 0 -> page 3` (the past: no file);
  `window 0 -> page 4` (act 2's present: no file), then `open L1S005`, `L1S009`, `L1S007`, `L1S006` (act 2's preload).
  The pictures: Palmtree Panic 1 present, past, Palmtree Panic 2 present.
- **Run 3 (16 MB, power on again, the cart kept):** `VERDICT no problems found`; `cart 16 MB (page mapper), 29 page
  switches; window 0 page 6, window 1 page 1 at the end`. The grep: `window 0 -> page 4`, `window 0 -> page 2`,
  `window 0 -> page 6` and **no `open L1S` at all** (all three scenes from the cart); one `open G1S006.MDG` (the good
  future's act file shares page 1's slot with the menu's font). The pictures: Palmtree Panic 2 present, past, good
  future.

## The test burn on your 4 MB cart (the new boot stack)

This disc is the first to run the changed `sub_main.c` on the console. On the 4 MB cart the game is R4D's; what is
new there:

1. Power on: the loading picture and the title as R4D (the boot loader is R4D's).
2. When the title starts, the game sends the new page command twice (window 1 to page 0, back to page 1): two writes
   to `0xA130F2`. Nothing to see: the title comes as always. If the loading picture stood and the title never came,
   it would point at this step.
3. A + B + C + Start at the title, Up twice: the last row reads `BIG CART   OFF`. A on it runs the cart test - two
   more page commands; the note rows read `CART TEST: NO PAGE MAPPER` / `(A 4 MB CART) - NOTHING WRITTEN`, the row
   stays `OFF`. This is the whole new path on the console: the SH2 -> the Sub-CPU's command 46 `0xF8nn` -> the
   68000's command 26 -> `0xA130F2`.
4. Then play as with R4D: `START GAME`, a save, Palmtree Panic 1, a warp, the next act. Everything loads from the
   CD as R4D (the second new case, `0xF0nn`, is never sent on the 4 MB cart).
5. Anything different from R4D: line 10's code if the text shows, or a photo.

## When the new cart exists

1. Burn R4G (an 80-minute CD-R).
2. Power on. With the 16 MB cart the Options Menu's last row reads `BIG CART   16 MB` (A + B + C + Start, Up twice to
   see it). Play as always: title, `START GAME`, a save.
3. **An act's start loads much longer than today**: the act's other three periods and the next act's start, each an
   image of about 1.1 MB and an act file of about 0.43 MB - up to about 6 MB, about 40 s at the CD's 150 KB/s plus the
   seeks (computed, not measured; less when a page already holds parts of the same zone). The first time ever also
   each act page's copy of the engine (5 x 400 KB, about 13 s more). The loading picture's bar shows it. Then warp:
   past, present - no loading picture, no disc, the act back after a moment. The next act starts the same way, no
   disc for its own scene (Time Attack too: each new act there pays its preload, the next act's start included).
4. Power off, power on, continue the same save: the act starts after the usual boot without its scene loads (a few
   seconds of checking the pages, the loading picture's bar standing).
5. Run the cart test too if you like (the row, A to turn it off, A again): `CART 16 MB: PAGES 2-7 OK`. It writes into
   the pages, so the next act start reloads what it overwrote (the engine's copies).
6. Anything odd: line 10's code if the text shows (5CB1 looking up, 5CB2 an engine copy, 5CB3 a page not usable, 5CB4
   a preload), or a photo.
