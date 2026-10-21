# R4K – faster disc loading: the boot stack changes (approved 20 Oct)

The game is R4J's (the engine, the SH2 boot loader and all game data are unchanged). The two changes are in the
**Sub-CPU / 68000 code**, the boot stack. Both come from the ares measurements of 20 Oct (MicroNut99/confidential,
`sonic-cd-ares-timing`).

## The two changes

1. **The root directory in Sub-CPU RAM** (`cd32x/kobo/sub_stubs.c`, `cd32x/kobo/scd.c`).
   - **Before:** every file open read the disc's root directory one sector at a time, and each read was a new seek.
     That cost about 1.7 s per open.
   - **Now:**
     - the first open after power-on reads the whole root directory (8 sectors) in one read;
     - every open after that looks the name up in RAM, with no disc access;
     - a name that is not in the root gets "not found" at once, as before, without the "/ROMS" search and the root
       listing (both of which read the disc again).
   - If the directory cannot be cached, every open takes the old path, as before. That happens when the directory is
     longer than 24 sectors, or when its first record is not ".".
2. **The 68000's copy into the cart: 4 bytes a step** (`cd32x/kobo/hw_md.s`, `chilly_rv_memcpy`, command 33).
   - **Before:** the copy went one byte at a time, about 42 clocks a byte. That was 0.7 s of the 2.0 s that each
     128 KB of a load took.
   - **Now:**
     - when the source, the destination and the length are all multiples of 4, it copies with `move.l`, about 9.5
       clocks a byte. Every load into the cart qualifies (16 KB pieces).
     - a `move.l` is two word writes to the 16-bit cart: the same bytes in the same places.
     - anything else takes the old byte loop. The 2-byte cart probe is one such case.
     - interrupts stay on and RV = 1, as before.

**No new command.** These are the same commands 38, 46 and 33, made faster.

## What changes against R4J

| | R4J | R4K |
|---|---|---|
| `cd32x/kobo/sub_stubs.c` | open_file reads the directory from the disc | the directory in RAM after the first open |
| `cd32x/kobo/scd.c` | not found → "/ROMS" search + root listing | not found → -1 at once (with the directory in RAM) |
| `cd32x/kobo/hw_md.s` | byte copy | 4-byte copy when aligned |
| `cd32x/sub_main.c`, `cd32x/sh2_engboot.c` | – | unchanged (the build label says R4K) |
| engine, game data, `SCDSH2.BIN` | – | unchanged: `R4K_expected.txt` = R4J's 340 lines |

**Expected, computed from the ares measurements (not yet measured):**
- a file open: about 1.7 s → near 0, after the first one, which reads the directory in about 0.3 s;
- each 128 KB of a load: about 2.0 s → about 1.45 s;
- R4J's first preload after the save screen: about 145 s in ares, so about 100 s.

**sh2emu does not run this code.** It plays the Sub-CPU and the 68000 itself, in C. Only ares and the console run it.

## Build (from the R4J build)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
md5sum -c --quiet R4J_expected.txt && echo "R4J build in place: all 340 OK"
unzip -o R4K_files.zip && md5sum -c --quiet R4K_md5.txt && echo "R4K_md5: all OK"
rm -f cd32x/build_engine/crt0.o cd32x/build_engine/sub_main.o cd32x/build_engine/hw_md.o cd32x/build_engine/scd.o cd32x/build_engine/scd_fs.o cd32x/build_engine/cd.o cd32x/build_engine/kos.o cd32x/build_engine/cdfs.o cd32x/build_engine/sub_stubs.o cd32x/build_engine/files.o cd32x/build_engine/crt.o cd32x/build_engine/crt.bin cd32x/build_engine/disc/APP.BIN cd32x/build_engine/SCDENG.iso cd32x/build_engine/SCDENG.bin cd32x/build_engine/SCDENG.cue
make -C cd32x ENGINE=1 START="0 0" ROUND=R4K
md5sum -c --quiet R4K_expected.txt && echo "R4K_expected: all 340 OK"
grep -ac "BUILD: SONIC CD R4K GAME START" cd32x/build_engine/disc/APP.BIN
grep -ac "SH2-VR4I" cd32x/build_engine/disc/APP.BIN
cp /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine/SCDENG.bin /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4K_NOMUSIC.bin
printf 'FILE "SONICCD_R4K_NOMUSIC.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4K_NOMUSIC.cue
```

If the first line does not print `all 340 OK`, the R4J build is not in place: run the R4J block
([R4J_NOTES.md](R4J_NOTES.md)) first.

The disc with music, for burning later:

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4K
```

## What to look for in ares

- **The boot:**
  - the green text as before, with `BUILD: SONIC CD R4K GAME START`;
  - "OPENING IMAGE.RAW..." to the loading picture should be shorter than before (R4G in ares: 2.7 s);
  - the bar fills faster.
- **The first trip to the menu, and the preload after the save screen:** the loading picture and its bar, as R4J, and
  shorter.
- **Things that would mean a fault in these changes:**
  - **"ERR: <file> NOT FOUND"** on the text screen: the directory lookup;
  - **a picture or a level drawn wrong after a load:** the copy, which would also show as STATE 5Cxx / a reload
    loop;
  - **a hang on the text screen before the loading picture.**
