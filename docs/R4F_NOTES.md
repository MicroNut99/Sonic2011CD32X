# R4F – the 16 MB game mode (BIG CART)

**Goal of the R4F disc:** the game ready for MicroNut99's 8 / 16 MB cart before the cart exists. The Options Menu's
last row is now **`BIG CART`**: it runs R4E's cart test and, when the test passes, keeps the time periods of the act
being played in the cart's pages - a warp to a period already in the cart is a page switch, **no CD read**. Off at
power-on: with `BIG CART` off the game is R4E's (= R4D's). **There is no reason to burn R4F for the 4 MB cart** - burn
it when the new cart exists. Checked in sh2emu only (its model of the mapper, R4E).

## How the pages are used: pairs

The scene images have the engine's native scripts linked to the place the image has in the cart today (`0x080000`):
moving the image (to window 1) would mean a second set of images, linked again. R4F does without - the cart is used as
**pairs of pages**, and each pair is a whole 4 MB cart exactly as the game knows it:

| Pair | Window 0 (cart `0x000000-0x1FFFFF`) | Window 1 (cart `0x200000-0x3FFFFF`) |
|---|---|---|
| 0 | page 0 (power-on) | page 1 (power-on) |
| 1 | page 2 | page 3 |
| 2 | page 4 (16 MB only) | page 5 |
| 3 | page 6 (16 MB only) | page 7 |

Each pair: the engine at `0x000000`, the scene image at `0x080000`, `SCDDATA.PAK` with the loading picture at
`0x300000`, the act file at `0x380000` - today's map. 16 MB = four pairs, 8 MB = two.

- **A scene a pair holds:** window 1 switched to the pair's odd page, window 0 to its even page (the cache purged after
  each switch). Window 0 shows the engine's code; every pair has its own copy of the same engine, so the SH2 runs on
  through the switch. The screen stays black for the change (as a restart after a death today), the planes come on.
- **A new period of the act being played:** into a pair that holds none of this act's periods (the least recently
  used one). With 16 MB all four periods of an act fit (present, past, good and bad future). With 8 MB (two pairs) the
  period being left always stays in: present ↔ past or present ↔ future switch without the CD, the third period
  replaces the one not in use.
- **A new act, the title, the menus, the special stages:** into the pair in use (the act just finished is not needed
  again).
- **A pair used the first time:** the engine (`SONICCD.PAK`) and `SCDDATA.PAK` are read into it first, through window 1
  (about 520 KB: about 3.5 s at 150 KB/s plus seeks - computed, not measured on the console), and the engine copy is
  compared byte for byte with the running engine before window 0 ever shows it. Once per pair until power-off. If it
  does not compare, line 10 shows `5CB1`-`5CB3` for a moment and the scene loads into the pair in use (the 4 MB way).

An act's periods are known from the scene numbers (`R11A`-`R11D` = regular list positions 0-3, act 2 = 4-7, act 3 =
8-9; Tails's images are other scenes).

## The boot stack

- **The Sub-CPU (`cd32x/sub_main.c`): one addition** (R4E's page switch is unchanged). Command 46 with COMM2 =
  `0xF000 | slot` (0-31) sets the cart slot (128 KB each) of the file command 38 just opened - the engine copy goes to
  slot 16 (cart `0x200000`, window 1) instead of slot 0, `SCDDATA.PAK` to slot 24 (`0x300000`, its own slot, through
  the other page). No new command number: a chunk run's COMM2 is at most `0x03FF`, R4E's page switch `0xF8xx`. Sent
  only by `BIG CART`.
- **The 68000 (`cd32x/kobo/hw_md.s`): unchanged.** **The SH2 loader (`cd32x/sh2_engboot.c`, `SCDSH2.BIN`): unchanged**
  (`SH2-VR4D`).

## BIG CART

The last row of the Options Menu (A + B + C + Start; from `START GAME`, Up twice). It reads `BIG CART   OFF  `.

- **A (or Start or Left / Right):** R4E's cart test runs (a few seconds, the screen still) and its two lines show under
  `SEGA CD32X` as before. When the test passes the row reads `BIG CART   16 MB` (or `8 MB`) and the mode is on from
  the next scene load. When it does not pass, the row stays `OFF` - the lines say why (R4E's table).
- **A again:** off (the row `OFF`; the next scene load goes back to pages 0 / 1, the 4 MB way).
- The test writes its patterns into pages 2-7: each time it runs, the pairs are emptied (they are filled again as
  scenes load).
- Off at power-on, every time (not saved).

## Checked before burning

- **sh2emu** (its model of the mapper: `-cart 4 / 8 / 16`), on the build made from scratch with the commands below -
  `VERDICT no problems found` in every run, no `STALE-READ`, no `CART-RV`, no `CART-W0` (sh2emu's new check: window 0
  switched to a page whose engine is not page 0's):

**Real time warps** (Palmtree Panic 1 from `STAGE SELECT` with `DEMO PAD` on; each warp started the game's own way -
`Warp.Destination` and `Warp.Timer` set while the scripted pad runs - the light flash, then the load):

| Warp | 16 MB | 8 MB |
|---|---|---|
| present → past | a new pair (1): the engine copy (checked) and `SCDDATA.PAK`, window 0 to page 2, then `L1S001.IMG` and `G1S001.MDG` | the same |
| past → present | **both windows to pair 0: no file read at all** (not the scene, not the act file); the act runs 63 frames later | the same |
| present → bad future | a new pair (2): the engine copy and `SCDDATA.PAK`, window 0 to page 4, then `L1S003.IMG` and `G1S003.MDG` | into pair 1 (the past goes, the present stays): `L1S003.IMG` and `G1S003.MDG` |
| future → present | **switch to pair 0: no file read** | the same |

  A fifth warp (to the past again) did not start in that scripted run (where the scripted pad had Sonic at the time),
  so it is not in the table. 63 frames (about a second) is the engine's own stage set-up; on the console the switch
  adds the two page writes and nothing else - no CD.

**Through the Options Menu** (A + B + C + Start, `STAGE SELECT` each time - the menu loads its font into the act file's
place, so in these runs a scene that comes back reads its act file again; the time warps above do not):

| Run | Result |
|---|---|
| 16 MB: `BIG CART` → `16 MB`; present, past, good future, bad future of Palmtree Panic 1, then present, good future, past, bad future | the first four each fill a pair (pairs 0-3, window 0 to pages 0 / 2 / 4 / 6); the last four: window switches, **no scene image read**. The pictures: the right period each time |
| 16 MB: present, past, Palmtree Panic 2 present, 2 past, 1 present, 2 present, then `BIG CART` off, 1 past | Palmtree Panic 2 present into the pair in use (a new act), 2 past into a new pair; 1 present still in pair 0 (switch); 2 present still in pair 1 (switch); off: back to pages 0 / 1, the past loaded there as the 4 MB game does. Window 0 page 0, window 1 page 1 at the end |
| 8 MB: `BIG CART` → `8 MB`; present, past, present, good future, present, past | two pairs: the present stays in pair 0, the past and the future take turns in pair 1 |
| 4 MB (today's cart): `BIG CART` | `CART TEST: NO PAGE MAPPER`, the row stays `OFF`; the stages load as R4E's (the test's two page commands only) |
| 16 MB, Tails: Sonic's present, Tails's present, past, present, Sonic's present | Tails's acts are other scenes (another act for the pairs): the same behaviour |
| Power-on to Palmtree Panic 1, `BIG CART` not touched (`-cart 16`) | as R4E: the same pictures (record CRC `2F19502C`, as R4E's run), the same loads (39 runs, 153 chunks + 9 one by one), 1 save, **no page switch** |

  sh2emu found its own bug on the way: after a page switch it still took the old scene's native scripts for the code
  (`EXEC OUTSIDE CODE` when the engine ran the other pair's scripts); it now reads them again after every switch.
- **The Sub-CPU's addition:** `sub_main.c` checked here with clang for the 68000 (the C only, no warnings - not
  assembled: your build uses gcc). Not run: sh2emu plays the Sub-CPU's commands itself (its model of `0xF0nn` does
  what the three lines do: the slot, COMM12 = 0, COMM0 = 0).
- **The delivery on its own:** R4D's files (your folder as it is) + `R4F_files.zip` + your Makefiles, built with the commands below (all but
  the 68000 part): every one of the 340 files in `R4F_expected.txt` the same.
- **Not run here:** your cart (the page writes through the 32X adapter, the pages themselves), the Sub-CPU's and the
  68000's code on the console, the CD's real speed (sh2emu reads the disc faster: its load times are not the console's).

## Build

In WSL, as R4E. `R4F_files.zip` holds every file changed since R4D - R4E's too: your folder is at R4D (`R4E_files.zip`
was not unpacked, and need not be). The only difference from R4E's commands: `ROUND=R4F`.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4F_files.zip && md5sum -c --quiet R4F_md5.txt && echo "R4F_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4F)
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/SFX*.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/ && python3 tools/make_cart_test.py cd32x/build_engine/disc/KOBOGFX.BIN
make -C cd32x ENGINE=1 START="0 0" ROUND=R4F
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music
md5sum -c --quiet R4F_expected.txt && echo "R4F_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4F GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4D" cd32x/build_engine/disc/APP.BIN                              # 1: the SH2 loader (unchanged since R4D)
```

Then sh2emu, before burning - the power-on run (as R4E), then `BIG CART` against the 16 MB model (present, past,
present):

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 6000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -bram /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin -shot 5800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 4000 -pad "1300:ABCS,1310:-,1450:U,1460:-,1470:U,1480:-,1500:A,1510:-,1600:D,1610:-,1630:D,1640:-,1660:D,1670:-,1700:A,1710:-,1730:A,1740:-,1760:S,1770:-,2200:ABCS,2210:-,2350:D,2360:-,2380:A,2390:-,2410:A,2420:-,2440:D,2450:-,2470:S,2480:-,3100:ABCS,3110:-,3250:D,3260:-,3280:A,3290:-,3310:A,3320:-,3340:S,3350:-" -cart 16 -shot 2170 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_big1.bmp -shot 3070 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_big2.bmp -shot 3970 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_big3.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_big.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_big.txt
grep -E "window 0|open L1S" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_big.txt
```

The first: `VERDICT no problems found`, `backup RAM SONICCD32X_: 3 loads, 1 saves`, Palmtree Panic 1 in the picture.
The second: `VERDICT no problems found`, `cart 16 MB (page mapper), 28 page switches; window 0 page 0, window 1 page 1
at the end`; the grep: `open L1S000.IMG`, `window 0 -> page 2`, `open L1S001.IMG`, `window 0 -> page 0` - and no
third `open L1S...` (the present came back from pair 0). The pictures: Palmtree Panic 1 present, past, present.

## When the new cart exists

1. Burn R4F (an 80-minute CD-R, as R4D).
2. Power on, A + B + C + Start, Up twice (`BIG CART`), A. Wait for the two lines (a few seconds).
3. `PAGES 2-7 OK` and the row `BIG CART   16 MB`: Down three times (`STAGE SELECT`), A, A (`REGULAR`), Start - Palmtree
   Panic 1. Warp to the past: the first time it loads from the CD (a little longer than today: the pair is filled
   first); warp back to the present - **no loading picture, no CD**: the act is back after a black moment.
4. Anything else in the two lines: tell me the two lines (a photo is fine). The game plays as R4E with the row `OFF`.
