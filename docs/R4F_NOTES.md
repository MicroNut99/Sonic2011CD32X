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

RESULTS

## Build

In WSL, as R4E. The only difference: `ROUND=R4F`.

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
md5sum -c --quiet R4F_expected.txt && echo "R4F_expected: all EXPECTEDCOUNT OK"
grep -ac "BUILD: SONIC CD R4F GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4D" cd32x/build_engine/disc/APP.BIN                              # 1: the SH2 loader (unchanged since R4D)
```

SH2EMU

## When the new cart exists

1. Burn R4F (an 80-minute CD-R, as R4D).
2. Power on, A + B + C + Start, Up twice (`BIG CART`), A. Wait for the two lines (a few seconds).
3. `PAGES 2-7 OK` and the row `BIG CART   16 MB`: Down three times (`STAGE SELECT`), A, A (`REGULAR`), Start - Palmtree
   Panic 1. Warp to the past: the first time it loads from the CD (a little longer than today: the pair is filled
   first); warp back to the present - **no loading picture, no CD**: the act is back after a black moment.
4. Anything else in the two lines: tell me the two lines (a photo is fine). The game plays as R4E with the row `OFF`.
