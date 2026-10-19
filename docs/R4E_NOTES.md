# R4E – the cart test for the 8 / 16 MB cart

**Goal of the R4E disc:** be ready for MicroNut99's new cart. The Options Menu gets a **`CART TEST`** row that tries
the cart's page mapper and reports what it found. Everything else plays as R4D. **There is no reason to burn R4E for
the 4 MB cart** - burn it when the new cart exists, and run the test before the game uses the pages (the 16 MB game
mode is the next step: `docs/CART16_HANDOFF.md`).

## The cart (your spec, 19 Oct)

Two windows of 2 MB - window 0 = cart `0x000000-0x1FFFFF`, window 1 = `0x200000-0x3FFFFF` - each showing one of 8
pages of 2 MB, selected with an upper-byte write (page in D8-D10) to `0xA130F0` (window 0) or `0xA130F2` (window 1),
with RV = 1 and interrupts off; window 0 = page 0 and window 1 = page 1 at power-on and reset (today's 4 MB); an 8 MB
build shows pages 4-7 as 0-3; the SH2 caches purged after a switch.

## The boot stack

- **The Sub-CPU (`cd32x/sub_main.c`): one addition.** Command 46 with COMM2 = `0xF800 | window << 8 | page` writes the
  page register. No new command number: a chunk run's COMM2 is at most `0x03FF`, so `0xF8xx` was never sent before.
  The game itself never sends it - only the `CART TEST` row.
- **The 68000 (`cd32x/kobo/hw_md.s`): unchanged.** The Sub-CPU uses the listener's existing command 26
  (`SAFE_CART_XFER`): interrupts off, RV = 1, a one-byte copy to `0xA130F0` / `0xA130F2`, RV = 0, interrupts back - your
  recipe exactly.
- **The SH2 loader (`cd32x/sh2_engboot.c`, `SCDSH2.BIN`): unchanged** (the same file as R4D's, `SH2-VR4D`).

## CART TEST

The last row of the Options Menu (A + B + C + Start; from `START GAME`, Up twice). A or Start runs it; it takes a few
seconds (CD reads), the screen stands still meanwhile, then the result is in the two lines under `SEGA CD32X`:

1. **Read only, first.** Window 1 is switched to page 0: if it then shows the same bytes as window 0, the switch works.
   Page 4: if it shows page 0's bytes, the cart is 8 MB. Each page to be tested that shows page 0's bytes is not
   written (it would be the engine's code). **Window 0 is never switched, pages 0 and 1 (the game's) never written.**
2. **Write.** Each of pages 2-7 (8 MB: 2-3) gets its own 32 KB pattern (chunk p of the test file `KOBOGFX.BIN`) at
   window 1 + p × 32 KB; the Sub-CPU reads each back through the 68000 and compares.
3. **Read back on the SH2.** Each page switched in again, the cache purged, the pattern read through the cache; and
   no page may show another page's pattern (nor page 1).
4. Window 1 back to page 1.

| The two lines | Meaning |
|---|---|
| `CART 16 MB: PAGES 2-7 OK` / `PAGE SWITCH OK, GAME PAGES UNTOUCHED` | the mapper works: the next disc can use the pages |
| `CART 8 MB: PAGES 2-3 OK` / ... | an 8 MB build (pages 4-7 = 0-3), working |
| `CART TEST: NO PAGE MAPPER` / `(A 4 MB CART) - NOTHING WRITTEN` | window 1 did not change: no mapper (today's cart), or the register writes do not reach it |
| `CART 16 MB: OK 2 3 4 6 7` / `BAD 5` | page 5 did not keep what was written (the 68000's read-back or the SH2's) |
| `... SAME RAM 4 6` | pages 4 and 6 show each other's patterns: a page bit not decoded |
| `... AS PAGE 0: 2 3` | those pages show page 0: not written (a page bit not decoded) |
| `CART TEST: KOBOGFX.BIN NOT ON THE DISC` | the test file is missing (the build's copy step) |

On your 4 MB cart the test writes the two page registers (`0xA130F0`, `0xA130F2`) and nothing else: a cart without
the mapper ignores them, and the result is `NO PAGE MAPPER`. (If your 4 MB cart decodes that address area for
something else, don't run it there.) After a test with problems, press reset before playing.

## Checked before burning

- **sh2emu** (its README: `-cart 4|8|16` models your mapper - two 2 MB windows, 8 pages, 8 MB aliasing; `-cartbad P`
  a page that keeps nothing; a cached line read after a switch without the purge shows as `STALE-READ`), on the
  build made from scratch with the commands below - `VERDICT no problems found` in every run:

| Run | Result |
|---|---|
| `CART TEST`, `-cart 4` (your 4 MB cart) | `CART TEST: NO PAGE MAPPER` / `(A 4 MB CART) - NOTHING WRITTEN`; 2 page commands (page 0, back to page 1), nothing written |
| `CART TEST`, `-cart 8` | `CART 8 MB: PAGES 2-3 OK` / `PAGE SWITCH OK, GAME PAGES UNTOUCHED`; 9 page switches |
| `CART TEST`, `-cart 16` | `CART 16 MB: PAGES 2-7 OK` / `PAGE SWITCH OK, GAME PAGES UNTOUCHED`; 21 page switches |
| `CART TEST`, `-cart 16 -cartbad 5` | `CART 16 MB: OK 2 3 4 6 7` / `BAD 5` |
| `CART TEST` on 16 MB, then `START GAME` | Palmtree Panic 1 plays (window 1 back on page 1): the game unaffected by the test |
| Power-on to Palmtree Panic 1 (`-cart 16`, the test not run) | as R4D: the title, the main menu, save select, Palmtree Panic 1; 1 save written; no page switch |

  No `STALE-READ` (the cache purge after each switch works) and no `CART-RV` (the SH2 off the cart while the 68000
  holds it) in any run. sh2emu found one real bug on the way: the test's first page switch was written while the pad
  request was still with the Sub-CPU and was lost - the test now waits for it, as the scene loads do.
- **The delivery on its own:** R4D's files + `R4E_files.zip` + your Makefiles, built with the commands below (all but
  the 68000 part): every one of the 340 files in `R4E_expected.txt` the same (the scene images, the act files, the
  sound banks, the engine, the loader - the same `SCDSH2.BIN` as R4D - and the test file `KOBOGFX.BIN`).
- **Not run here:** your cart. The page register writes through the 32X adapter, the RV timing and the pages
  themselves are what the test on the new cart shows.

## Build

In WSL, as R4D. Two differences: the test file (`tools/make_cart_test.py`, in the copy step) and `ROUND=R4E`.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4E_files.zip && md5sum -c --quiet R4E_md5.txt && echo "R4E_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4E)
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/SFX*.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/ && python3 tools/make_cart_test.py cd32x/build_engine/disc/KOBOGFX.BIN
make -C cd32x ENGINE=1 START="0 0" ROUND=R4E
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music
md5sum -c --quiet R4E_expected.txt && echo "R4E_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4E GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4D" cd32x/build_engine/disc/APP.BIN                              # 1: the SH2 loader (unchanged since R4D)
```

Then sh2emu, before burning - the power-on run (as R4D), then the cart test against the 16 MB model:

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 6000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -bram /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin -shot 5800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 1800 -pad "1300:ABCS,1320:-,1450:U,1460:-,1470:U,1480:-,1500:A,1510:-" -cart 16 -shot 1750 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_carttest.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_cart.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_cart.txt
```

The first: `VERDICT no problems found`, `backup RAM SONICCD32X_: 3 loads, 1 saves`, Palmtree Panic 1 in the picture.
The second: `VERDICT no problems found`, `cart 16 MB (page mapper), 21 page switches; window 0 page 0, window 1 page 1
at the end`, and the picture: the Options Menu with `CART 16 MB: PAGES 2-7 OK` / `PAGE SWITCH OK, GAME PAGES
UNTOUCHED` under `SEGA CD32X`.

## When the new cart exists

1. Burn R4E (an 80-minute CD-R, as R4D).
2. Power on, A + B + C + Start, Up twice (`CART TEST`), A. Wait for the two lines (a few seconds).
3. `PAGES 2-7 OK`: the cart works with this software - the 16 MB game mode can be built for it. Anything else: tell
   me the two lines (a photo is fine).
4. The game plays as R4D either way (the test leaves window 1 on page 1).

