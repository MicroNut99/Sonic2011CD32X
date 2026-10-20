# R4I – ready to play after a power-on: the warm start and the menu in the cart

**Goal:** with the 16 MB cart (battery-backed), nothing that is already in the cart comes off the CD again. Every
area of the cart is checked by checksum against the disc's own table, and only an area that fails is loaded.

- **Power-on (the SH2 boot loader, `cd32x/sh2_engboot.c`):**
  - it reads the data pack `SCDDATA.PAK` (about 130 KB) first;
  - the pack's preload table holds the disc's checksums of the engine and the title;
  - if the engine and the title already in the cart give the same checksums, they are not read from the CD;
  - on any mismatch, or on a cart that kept nothing (the 4 MB cart, a first power-on), they load exactly as before.
- **The menu / save screen** (`L0S001`, 2.2 MB) has its own pages, 6 and 7, with its own copy of the engine. Title →
  menu → title is a page switch. After a power-on, the menu's pages are found by header and checksum, like the act
  pages.
- **Act pages:** four instead of five, to make room for the menu. They hold the act's present, its past, the next
  act's start and its bad future. The good future, the rare one, loads from the CD when you warp there.
- **Title demos:** a demo whose scene an act page already holds plays from there. Any other demo plays in page 0, as
  before; the title is then loaded again after it.

Checked in sh2emu with the cart kept between two power-ons (`-cartkeep`). On the second power-on the only files read
are the loading picture and the data pack; the title, the menu and Palmtree Panic 1 all came from the cart.

## The boot stack

- **`cd32x/sh2_engboot.c` (the SH2 boot loader) changes, against R4D, for the first time.** Label `SH2-VR4I`.
  - It loads the data pack first, then the engine, then the title. The engine and the title are skipped when the
    cart holds them, using the checksum the loader already uses for its loads (`scd_cart_sum`).
  - **No new command**: only commands 38 and 46, as before.
- **`cd32x/sub_main.c`:** as R4G/R4H (the two command-46 cases, run on your console). **`hw_md.s`:** R4D's.

## Pages (16 MB)

| Page | Window | Holds |
|---|---|---|
| 0, 1 | 0, 1 | as the 4 MB game: the engine, the title (the boot loader's), the other non-act scenes, the data pack (0x300000), act file slot 0 (0x380000, shared with the Options Menu's font), act file slot 3 (0x260000: a scene shown from page 0 that is longer than the title runs over it, and that act file is loaded again) |
| 2-5 | 0 | the act pages: an engine copy and one act scene each |
| 6 | 0 | the menu: an engine copy and the menu's first 1.5 MB |
| 7 | 1 | the menu's last 0x98000 bytes, act file slots 1 (0x300000) and 2 (0x380000) |

## Build

The steps are the same as R4H's, with three differences:
- `build_preload.py` now also takes the engine;
- `ROUND=R4I`;
- the loader's label is `SH2-VR4I`.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4I_files.zip && md5sum -c --quiet R4I_md5.txt && echo "R4I_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
python3 tools/build_preload.py game_data cd32x/build_engine/engine.bin
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/SFX*.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/ && python3 tools/make_cart_test.py cd32x/build_engine/disc/KOBOGFX.BIN
make -C cd32x ENGINE=1 START="0 0" ROUND=R4I
mkdir -p /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4I
md5sum -c --quiet R4I_expected.txt && echo "R4I_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4I GAME START" cd32x/build_engine/disc/APP.BIN
grep -ac "SH2-VR4I" cd32x/build_engine/disc/APP.BIN
```

`build_preload.py` prints `SCDDATA.PAK: the preload table, 283 entries (142 images, 140 act files, 1 engine)`.

## sh2emu before burning: power on twice with the cart kept

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_i1_menu.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_i1_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_i1.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_i1.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_i2_menu.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_i2_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_i2.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_i2.txt
grep -c "CMD 38 open" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_i2.txt
```

Both runs must end `VERDICT no problems found`. The second power-on opens only `IMAGE.RAW` and `SCDDATA.PAK`, so
the grep prints `2`. The pictures: the main menu, then Palmtree Panic 1.

## What to expect (ares with the 16 MB cart, or the real cart)

1. **First power-on with this disc:** loads as before. The first time to the menu, the menu goes into its pages: a
   normal load. At the act's start comes the preload: the long bar.
2. **Power-off, power-on:** the loading picture, and the bar fills quickly with no CD loading. Then the title. The
   first trip to the menu has a few seconds of checking (the bar standing); then the menu appears, and your save's act
   starts with no loading.
3. **Title ↔ menu, warps, restarts:** page switches.
4. **The 4 MB cart:** if it keeps its contents through a power-off, the engine and the title are skipped there too;
   if it doesn't, every power-on loads as R4D. The menu and the act pages need the 16 MB cart.
