# R4N – the pause menu says MENU

R4M plus one change: the pause menu's third item reads **MENU** instead of EXIT. It still opens the level select
(R4M).

- **Where the word comes from:** the pause screen's words are pictures in `Data/Sprites/Global/Pause_EN.gif`. That
  sheet already has "MENU" inside its "DEV MENU" word (x 85, y 81, 36 × 11).
- **The change:** `engine/Script.cpp` (`patches/engine.patch`, `FUNC_SPRITEFRAME`). The EXIT frame (30 × 11 at 66, 69,
  pivot 0, -5) is pointed at those letters when the pause object sets up its frames.
- **Where the frame ends up:** the PC build writes the frame into the cart images and the act files' sprite packs, and
  the SH2 build uses them from there. Nothing else changes, and the Options Menu and the boot stack are R4M's / R4K's.

**Checked in sh2emu** (the full build from scratch): Palmtree Panic 1 → START → down twice shows CONTINUE, RESTART,
MENU, with no problems.

## Build (from the R4K or R4L build)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4N_files.zip && md5sum -c --quiet R4N_md5.txt && echo "R4N_md5: all OK"
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
make -C cd32x ENGINE=1 START="0 0" ROUND=R4N
md5sum -c --quiet R4N_expected.txt && echo "R4N_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4N GAME START" cd32x/build_engine/disc/APP.BIN
grep -ac "SH2-VR4I" cd32x/build_engine/disc/APP.BIN
cp /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine/SCDENG.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4N_NOMUSIC.bin
printf 'FILE "SONICCD_R4N_NOMUSIC.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4N_NOMUSIC.cue
python3 tools/build_audio_disc.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4N_NOMUSIC.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4N
```

sh2emu before burning: R4L's two power-ons ([R4L_NOTES.md](R4L_NOTES.md)) with `l1` / `l2` read as `n1` / `n2`. Both
runs must end `VERDICT no problems found`; the opens are 18, then 2.

## What to expect

- **The Options Menu:** 2ND SH2 ON, STAGE SFX ON, UNDERWATER ON.
- **In an act, Pause:** CONTINUE, RESTART, MENU. **MENU:**
  - the loading picture briefly (the title goes back into page 0);
  - then SELECT A STAGE LIST, with REGULAR picked;
  - A: the list's stages; B: the Options Menu.
- **The first power-on with R4N:** everything loads once before the title (R4L). After that there is no loading for the
  menu or the first act.
