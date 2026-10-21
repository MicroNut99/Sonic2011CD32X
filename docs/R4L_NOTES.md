# R4L – the boot preload: everything a new game needs is loaded before the title

**The owner's rule:** the menu / save screen and a new game's first act are loaded in advance, at the very first
power-on. After that, nothing for them comes off the disc. A save of another act loads its act set when that act
starts, as before.

**R4K** loaded the menu on its first trip and the act set at the act's start. **R4L** loads both at power-on, before
the title, under the loading picture and its bar.

## What happens at power-on (16 MB cart) – `big_boot_preload`, `cd32x/engine/plat_mars.cpp`

1. The boot loader loads or skips the engine and the title (R4I's warm start, unchanged).
2. The engine looks every page up: the header fingerprint, then the checksum over the length (R4I's `big_identify`).
3. It loads only what the cart does not hold whole:
   - **the menu / save screen** (`L0S001`, pages 6 and 7) and the engine copy in page 6;
   - **Palmtree Panic 1's act set:** its present, past, act 2's start and bad future, each with its act file and its
     engine copy. This set loads only when no act page holds an act scene at all: the first power-on, a new build's
     first power-on, or after the cart test. An act kept from the last time played stays, because that is what the
     save goes on with.
4. When everything is already there, only the engine copies are compared. A page whose copy differs gets the engine
   again.

Then the title, the menu, the save screen and a new game's Palmtree Panic 1 are page switches.

- **Line 10:** `5CB5` while the boot preload runs.
- **The 8 MB and 4 MB carts:** no boot preload, as before.

## The boot stack

**No boot stack change from R4K:** `sub_main.c`, `kobo/sub_stubs.c`, `kobo/scd.c`, `kobo/hw_md.s` and `sh2_engboot.c`
(label `SH2-VR4I`) are R4K's. The one changed file is `cd32x/engine/plat_mars.cpp`.

The engine is different, so its checksum and every scene image's native scripts are different too. So the first
power-on with R4L loads everything once, now all of it at power-on.

## Checked

**sh2emu** (two power-ons, the cart kept): both runs end `VERDICT no problems found`.
- **The first power-on:**
  - the boot preload loads the menu and the four act pages before the title;
  - then title → menu → save screen → Palmtree Panic 1 are page switches, with no file opened.
- **The second power-on:** only `IMAGE.RAW` and `SCDDATA.PAK` are read. The pages are checked in about 4 s, then the
  title.

**ares** (MicroNut99/ares_android, R4K's data track with R4L's engine and data, without music, a fresh 16 MB cart):

| | first power-on | second power-on |
|---|---|---|
| files read | `IMAGE.RAW`, `SCDDATA.PAK`, the engine, the menu, the four act scenes, their act files and engine copies | `IMAGE.RAW`, `SCDDATA.PAK` |
| the title running | about 196 s | about 44 s |
| title → menu → save screen → Palmtree Panic 1 | page switches, nothing read | page switches, nothing read |
| the title demo (Palmtree Panic 1) | from its act page, nothing read | from its act page, nothing read |

## Build (from the R4K build)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4L_files.zip && md5sum -c --quiet R4L_md5.txt && echo "R4L_md5: all OK"
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
make -C cd32x ENGINE=1 START="0 0" ROUND=R4L
md5sum -c --quiet R4L_expected.txt && echo "R4L_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4L GAME START" cd32x/build_engine/disc/APP.BIN
grep -ac "SH2-VR4I" cd32x/build_engine/disc/APP.BIN
cp /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine/SCDENG.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4L_NOMUSIC.bin
printf 'FILE "SONICCD_R4L_NOMUSIC.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4L_NOMUSIC.cue
python3 tools/build_audio_disc.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4L_NOMUSIC.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4L
```

The last line makes the disc with music, `discs/SONICCD_R4L.bin` / `.cue` (an 80-minute CD-R). It can be left out for
ares.

## sh2emu before burning: power on twice with the cart kept

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_l1_menu.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_l1_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_l1.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_l1.txt
grep -c "CMD 38 open" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_l1.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_l2_menu.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_l2_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_l2.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_l2.txt
grep -c "CMD 38 open" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_l2.txt
```

Both runs must end `VERDICT no problems found`.
- **The first grep prints 18:** all 18 file opens happen before the title.
- **The second prints 2:** `IMAGE.RAW` and `SCDDATA.PAK`.
- **The pictures:** the main menu, then Palmtree Panic 1.

## What to expect (ares or the console, the 16 MB cart)

1. **The first power-on with R4L:**
   - the loading picture stays up for a few minutes while the bar fills (in ares, the title at about 196 s);
   - then the title, and the menu, the save screen and a new game's first act start with no loading.
2. **Every later power-on:**
   - the bar fills quickly with no disc reading;
   - the title, about 44 s from power-on in ares;
   - then no loading for the menu or the act the save goes on with.
3. **A saved game in another act:** its act set loads once at that act's start (the loading picture). Later power-ons
   keep it.
