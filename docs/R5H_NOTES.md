# R5H – faster special stages

The owner's request (21 Oct, after R5G ran on the console): "The special stages are looking ALOT better. Is there no
other way to speed it up?"

R5H is built on R5G.

## What changed

**1. The chip draws the floor's far rows too.**
- Before, the SH2 drew every floor row that reached past the map's edge. Behind those rows the sky layers had to show,
  and the 32X can only draw in front of the Genesis.
- Now the SH2 draws only the rows the chip cannot: those whose positions leave the chip's 13 bits
  (`flr_chip_row`, the Sub-CPU's own test).

**2. The layers below line 120 sit behind the chip's floor.**
- Special stages 2-8: layer 1's rows below line 120 are on plane B, rows 15-27 (`tools/build_floor.py`). Plane B
  shows wherever the floor is see-through, as in the engine. The 32X draws none of layer 1.
- Floors 2, 3, 6 and 7 are 58-82% see-through. R5G showed black there; R5H shows the stage's own ground, sea or grid.
- Special stage 1 (layer 3, the water under the swirl): the 32X draws it below line 120 in **behind colours**. These
  are CRAM slots 63-95 and 100-127 written without the through bit, so the chip's floor is in front of them. No
  picture in `Sprites/Special`, `Global` or `Players` uses those slots.
- Layer 2's lines 120-127 (the horizon's bottom edge) stay the 32X's, in front of the Genesis. The pixels where the
  floor's map is are left out.

**3. The 32X picture is cleared only where the last picture drew.**
- Each frame buffer keeps a record at `+1F800` of what its last picture drew: for each band of 4 lines, the x range;
  for each line, flags.
- The picture's first draw clears just that.
- **The record is kept in 16-bit words.** The 32X ignores a byte write of 0 into the frame buffer (ares models this;
  sh2emu now does too).

**4. Less waiting for the Sub-CPU.**
- Command 80 op 1 (the floor's X and Z) is sent at the picture's first draw.
- Op 4 (the angle) is sent without waiting. Its answer is read before the next command, or at the picture's end.
- The Sub-CPU answers op 4 before it writes the chip's trace table (100 lines of 32-bit arithmetic on its 68000).

**5. Less work in a frame that is not drawn.**
- No line scroll is computed for the layers (`plat_layer_lines` gives none).
- No floor scan is done.
- The floor's first in-map row is computed once a frame.

## Speed (sh2emu, pictures a second, frames 2800-3400 of each special stage run)

| | R5G | R5H |
|---|---|---|
| Special stage 1 | 19.9 | 29.9 |
| Special stage 2 | 29.9 | 29.9 |
| Special stage 3 | 28.4 | 29.3 |
| Special stage 4 | 30.0 | 30.0 |
| Special stage 5 | 15.5 | 27.9 |
| Special stage 6 | 14.9 | 29.9 |
| Special stage 7 | 29.9 | 57.5 |
| Special stage 8 | 23.4 | 28.9 |

These are measured during play. R5G_NOTES' table used each run's last 10 s, which for stage 7 is its finish screen.

Stage 7 reaches 57.5 because the 32X now draws almost nothing there: no layers, few sprites.

## The boot stack

- **`cd32x/sub_main.c`, command 80 op 4:** the answer (COMM12) and COMM0 = 0 come before the trace table is written and
  the chip started. Nothing else changed in it.
- **The 68000 listener (`kobo/hw_md.s`) is unchanged.**
- `kobo/sub_stubs.c`, `kobo/scd.c` and `sh2_engboot.c` (`SH2-VR4I`) are unchanged.
- **`FLOOR2.BIN` - `FLOOR8.BIN`:** plane B's name table now covers rows 0-27, and its cells grow by at most 38 (stage 2:
  232 to 270). The format is unchanged (the sky part's block list); the Sub-CPU copies whatever blocks are listed.

## The changed files (since R5G)

| File | Change |
|---|---|
| `cd32x/engine/plat_mars.cpp` | the record and the clear; the behind colours; the floor scan; ops 1 and 4 not waited for; plane B layers |
| `cd32x/engine/scd_pm.h` | the drawn-rectangle hooks; a layer drawn through a colour table, with a pixel range left out |
| `cd32x/sub_main.c` | command 80 op 4 answered first |
| `tools/build_floor.py` | plane B rows 0-27 for special stages 2-8 |
| `tools/sh2emu/sh2emu.c` | a byte write of 0 into the frame buffer is dropped (the 32X's rule) |

## Build (from the R5G build)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R5H_files.zip && md5sum -c --quiet R5H_md5.txt && echo "R5H_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
python3 tools/build_floor.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data
python3 tools/build_preload.py game_data cd32x/build_engine/engine.bin
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/SFX*.BIN game_data/IMAGE.RAW game_data/FLOOR*.BIN cd32x/build_engine/disc/ && python3 tools/make_cart_test.py cd32x/build_engine/disc/KOBOGFX.BIN
make -C cd32x ENGINE=1 START="0 0" ROUND=R5H
md5sum -c --quiet R5H_expected.txt && echo "R5H_expected: all 349 OK"
grep -ac "BUILD: SONIC CD R5H GAME START" cd32x/build_engine/disc/APP.BIN
grep -ac "SH2-VR4I" cd32x/build_engine/disc/APP.BIN
ls cd32x/build_engine/disc/FLOOR*.BIN | wc -l
```

**What each check prints:**
- `R5H_md5: all OK`
- `R5H_expected: all 349 OK`
- **1** for each of the two `grep -ac` lines
- **9** for the last line

`build_floor.py` prints a line for each file. For stage 2 it reads
`FLOOR2.BIN: SS2 289 stamps, 13 colours; sky: 2 colour lines, 16 colours, plane A 35 cells, plane B 270 cells`.

## sh2emu before burning

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
rm -f /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_c1_menu.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_c1_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c1.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c1.txt
grep -c "CMD 38 open" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c1.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 9000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -cart 16 -cartkeep /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_cart16.bin -shot 2000 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_c2_menu.bmp -shot 8800 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_c2_ppz1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c2.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c2.txt
grep -c "CMD 38 open" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c2.txt
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 3400 -pad "1400:ABCS,1410:-,1550:D,1560:-,1600:A,1610:-,1700:D,1710:-,1760:A,1770:-,1860:A,1870:-" -cart 16 -shot 3300 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_c3_ss1.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c3.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c3.txt
grep "CMD 80 floor begin" /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log_c3.txt
```

**What to look for:**
- All three runs must end `VERDICT no problems found`.
- The two `CMD 38 open` counts: **18**, then **2**.
- The third run (special stage 1):
  - It shows both `floor begin` lines: the title's clouds (9), then special stage 1.
  - Its `pictures` line reads about **30** a second.
- Up to 4 `FB-BYTE0` lines appear around frame 395, at `+1FE88` - `+1FE8B`. They come from the engine's own one-line
  buffer (`SCD_FB_LINE`), as in every build since R4; nothing reads them back. They are not counted as problems.

## What to expect on the console

- **The special stages run at about 30 pictures a second;** stage 7 runs faster.
- **Through the floor's see-through parts you now see each stage's own ground** (blue sea in 2, pink water in 6, the
  pink grid in 7), where R5G showed black.
- **The title is unchanged** (sh2emu: 18.2 pictures a second, R5G 18.1).
- **If a special stage shows ghost copies of Sonic or the UFOs, or black blocks:** the clear's record did not work on
  the console. Send a photo.
