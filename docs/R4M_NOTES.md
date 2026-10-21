# R4M – the Options Menu switches on at power-on, Pause > EXIT is the level select

The owner's requests (20 Oct):
1. **On by default:** 2ND SH2, STAGE SFX, UNDERWATER.
2. **Pause > EXIT goes to the level select.**

## What changed against R4L

| File | Change |
|---|---|
| `patches/engine.patch` (`engine/Debug.cpp`, `engine/plat.hpp`) | `scdSh2Switch`, `scdSfxSwitch`, `scdWaterSwitch` start at 1. The Options Menu shows them ON and A / left / right still turns each one off. The new `scdMenuStages`: when set, the menu opens at STAGE SELECT's stage lists (`scd_stage_lists`); B there gives the Options Menu, as before. |
| `cd32x/engine/plat_mars.cpp` | R4H's pause EXIT path (`toOptions`, line 10 `5C19`) sets `scdMenuStages`. The title is still loaded into page 0 first, as R4H. |

What each switch does (R4D):
- **2ND SH2:** the second SH2 draws half of each 32X-picture-mode picture: the special stages, the title, the menus.
- **STAGE SFX:** each scene's own sound bank (`SFX<n>.BIN`) is loaded into the PCM chip at its load.
- **UNDERWATER:** the act's underwater colours below the water line on the Genesis planes.

A game over also goes back through the title. That has been the same path as Pause > EXIT since R4H, so it now lands
on the level select too.

**No boot stack change from R4K.** The engine changed, so its checksum and every scene image's native scripts are new.
The first power-on with R4M loads everything once, at power-on (R4L's boot preload).

## Build (from the R4K or R4L build)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4M_files.zip && md5sum -c --quiet R4M_md5.txt && echo "R4M_md5: all OK"
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
make -C cd32x ENGINE=1 START="0 0" ROUND=R4M
md5sum -c --quiet R4M_expected.txt && echo "R4M_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4M GAME START" cd32x/build_engine/disc/APP.BIN
grep -ac "SH2-VR4I" cd32x/build_engine/disc/APP.BIN
cp /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine/SCDENG.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4M_NOMUSIC.bin
printf 'FILE "SONICCD_R4M_NOMUSIC.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4M_NOMUSIC.cue
python3 tools/build_audio_disc.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4M_NOMUSIC.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4M
```

sh2emu before burning: R4L's two power-ons ([R4L_NOTES.md](R4L_NOTES.md)) with `l1` / `l2` read as `m1` / `m2`. Both
runs must end `VERDICT no problems found`; the opens are 18, then 2.

## What to expect

- **The Options Menu:** 2ND SH2 ON, STAGE SFX ON, UNDERWATER ON.
- **In an act, Pause > EXIT:**
  - the loading picture briefly (the title goes back into page 0);
  - then SELECT A STAGE LIST, with REGULAR picked;
  - A: the list's stages; B: the Options Menu.
- **The first power-on with R4M:** everything loads once before the title (R4L). After that there is no loading for the
  menu or the first act.
