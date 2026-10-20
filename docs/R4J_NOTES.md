# R4J – the loading picture after the save screen

**The bug (R4I):** after the save choice the screen stayed on the faded Save Select. It looked like a hang, but the
game was loading the act's preload behind it. In ares, at CD speed, Palmtree Panic 1 started about 2.5 minutes later,
on its own.

**Why:** the menu's pages are new in R4I. The save screen's image runs from page 6 into page 7, so while it shows,
window 1 is on page 7. The loading picture comes from the data pack in page 1 (cart 0x300000). When a scene in the 32X
picture mode ends (`pm_end`), the picture was drawn *before* window 1 was switched back to page 1. It read page 7,
found no picture there, and left the last frame of the menu on screen. sh2emu showed the same frozen frame, but there
the loads take a few frames, so it went unnoticed.

**The fix:** `pm_end` switches window 1 to page 1 (`big_home_w1`, the same call `planes_off` makes) before it draws
the picture. Nothing else changes.

## The boot stack

**No boot stack file changes from R4I**: `sh2_engboot.c` (`SH2-VR4I`), `sub_main.c` and `hw_md.s` are R4I's.
`ROUND=R4J` only changes the build label text. The one changed file is `cd32x/engine/plat_mars.cpp` (the engine).

The engine is different, so its checksum is different. The data pack's preload table therefore has a new engine
checksum, and every scene image has new native scripts. The first power-on with R4J loads the engine, the title, the
menu and the act pages again (the warm-start checks fail on the old copies). After that, power-ons are warm again.

## Build (from the R4I folder)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4J_files.zip && md5sum -c --quiet R4J_md5.txt && echo "R4J_md5: all OK"
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
make -C cd32x ENGINE=1 START="0 0" ROUND=R4J
mkdir -p /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4J
md5sum -c --quiet R4J_expected.txt && echo "R4J_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4J GAME START" cd32x/build_engine/disc/APP.BIN
grep -ac "SH2-VR4I" cd32x/build_engine/disc/APP.BIN
```

## sh2emu before burning

These are the same two power-ons as R4I, plus one picture during the preload (frame 3450).

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_j1_menu.bmp -shot 3450 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_j1_load.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_j1_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_j1.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_j1.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_j2_menu.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_j2_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_j2.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_j2.txt
grep -c "CMD 38 open" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_j2.txt
```

Both runs must end with `VERDICT no problems found`, and the grep must print `2`. `shot_j1_load.bmp` must be the
loading picture with its bar, not the save screen.
