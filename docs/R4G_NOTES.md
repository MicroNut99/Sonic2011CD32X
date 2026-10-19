# R4G – the preload (the 16 MB cart)

**Goal of the R4G disc:** with MicroNut99's 16 MB cart (battery-backed: what is in it stays through power-off), the
act you play is in the cart **with every time period and the next act's start**, so warps and the next act need no
CD, and after a power-off the game is **ready to play** - unless you continue a save in another act. Nothing changes
on the 4 MB cart. Checked in sh2emu only (its model of the mapper, R4E, and now of the battery).

R4F's "pairs" are gone: they held 4 scenes, and the boot loader's title took the first one at every power-on.

## How the cart is used

| Pages | Window | Holds |
|---|---|---|
| 0 and 1 | 0 and 1 | as the 4 MB game: the engine, the boot loader's title, the menus, the special stages, the title's demos, the data pack with the loading picture (0x300000), one act file slot (0x380000) |
| 2-6 | 0 | the **act pages**: each its own copy of the engine (the same as the running one, byte for byte) and one act scene's image at 0x080000, used where it lies |
| 7 | 1 | the act files of act pages 2-5, a 512 KB slot each; page 6's goes into page 1's slot |

- **A scene in an act page:** window 0 to its page, window 1 to its act file's page - two page switches, no disc.
- **An act's start** (its first scene - from a save, the menu, or the end of the act before; not a title demo): the
  act's other time periods and **the next act's start** are loaded into the other act pages, with the loading
  picture and its bar, the music off (a CD data read stops it anyway). After act 1: act 2's present; after act 2:
  act 3's bad future (the usual one - the good future loads at its start, as today); after act 3: the next zone's
  act 1. Then the act starts as always. The order, the most needed first: the present, the past, the next act's
  start, the bad future, the good future. The last one goes into page 6, whose act file shares page 1's slot with the
  title demos, the credits and the dev menu's font: after those, only that period's act file is read again (about
  400 KB: near 3 s at 150 KB/s, computed), never its image.
- **Warps and restarts** inside the act: page switches only.
- **After a power-on** the game knows nothing about the pages: the first act scene looks each act page and act file
  slot up in the **preload table** (`tools/build_preload.py`: in `SCDDATA.PAK`, which the boot loader brings in at
  every power-on) by its header's fingerprint, and checks it whole with the loader's checksum. A page that was half
  loaded when the power went off does not match and is loaded again. A few seconds of checking, no disc.
- **Title demos** play in page 0, as on the 4 MB cart: they never push your act out.
- **8 MB cart:** one act page (2), its act file in page 3: the scene you play stays through a power-off.
- **4 MB cart, or `BIG CART` off:** none of this - the game is R4E's (= R4D's).

The 16 MB cart is found at power-on (read only: window 1 switched to page 0, then to page 4, compared, back to page
1). On the 4 MB cart these are two writes to the page register `0xA130F2` (window 1 to page 0, back to page 1), as
R4E's `CART TEST` does: a cart without the mapper ignores them.

## The Options Menu

The last row reads `BIG CART   16 MB` (or `8 MB`) when the cart was found, `OFF` otherwise. A turns it off (the 4 MB
way); A again runs R4E's cart test and, if it passes, turns it on. The test writes into pages 2-7, so the act pages
are looked up again at the next act.

## The boot stack

As R4F - nothing new in R4G. Against R4D (your last disc), `cd32x/sub_main.c` has R4E's and R4F's two additions, both
under command 46 and both used only with the 8 / 16 MB cart: `0xF8nn` writes a page register (through the 68000's
existing command 26, RV = 1, interrupts off) and `0xF0nn` puts the opened file into another 128 KB slot. The 68000's
`hw_md.s` and the SH2 loader (`SCDSH2.BIN`, `SH2-VR4D`) are R4D's. The power-on still reads the engine, the data pack
and the title from the disc, as today.

## Checked before burning

RESULTS

## Build

In WSL. `R4G_files.zip` holds every file changed since R4D (R4E's and R4F's too: your folder is at R4D - `R4E_files.zip`
and `R4F_files.zip` need not be unpacked). Against R4F's commands: one more step after `build_mdg.sh` (the preload
table into `SCDDATA.PAK`), `ROUND=R4G`, and the disc image under its own name in `discs/`. The disc needs an 80-minute
CD-R, as R4D.

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
unzip -o R4G_files.zip && md5sum -c --quiet R4G_md5.txt && echo "R4G_md5: all OK"
rm -rf engine && sh tools/setup_engine.sh && sh tools/check_patch.sh     # engine/ = RSDKv3 decompilation + patches/engine.patch (R4G)
make -C host clean && make -C host
rm -rf game_data
python3 tools/build_audio.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data
sh tools/build_native.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk
rm -rf cd32x/build_engine && make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin
python3 tools/build_game_data.py /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data --scenes "0:*,1:*,1:*@1,3:*,3:*@1" --start 0:0 --native cd32x/build_engine
sh tools/build_mdg.sh /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X/Sonic_CD/Data.rsdk game_data "1:*,1:*@1"
python3 tools/build_preload.py game_data
mkdir -p cd32x/build_engine/disc && cp game_data/KOBOSFX.BIN game_data/SFX*.BIN game_data/IMAGE.RAW cd32x/build_engine/disc/ && python3 tools/make_cart_test.py cd32x/build_engine/disc/KOBOGFX.BIN
make -C cd32x ENGINE=1 START="0 0" ROUND=R4G
mkdir -p /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs
python3 tools/build_audio_disc.py cd32x/build_engine/SCDENG.bin game_data/music --out /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/discs/SONICCD_R4G
md5sum -c --quiet R4G_expected.txt && echo "R4G_expected: all EXPECTEDCOUNT OK"
grep -ac "BUILD: SONIC CD R4G GAME START" cd32x/build_engine/disc/APP.BIN       # 1: the round's label is in
grep -ac "SH2-VR4D" cd32x/build_engine/disc/APP.BIN                              # 1: the SH2 loader (unchanged since R4D)
```

`python3 tools/build_preload.py game_data` prints `SCDDATA.PAK: the preload table, 280 entries (140 images, 140 act
files)`. Burn `discs/SONICCD_R4G.cue`.

SH2EMU

## When the new cart exists

1. Burn R4G (an 80-minute CD-R).
2. Power on. With the 16 MB cart the Options Menu's last row reads `BIG CART   16 MB` (A + B + C + Start, Up twice to
   see it). Play as always: title, `START GAME`, a save.
3. **The first act start loads longer than today**, once: the act's four periods and the next act's start, with
   the loading picture's bar (the first time ever also each page's copy of the engine). Then warp: past, present -
   no loading picture, no disc, the act back after a moment.
4. Power off, power on, continue the same save: the act starts after the usual boot without its scene loads (a few
   seconds of checking the pages, the loading picture's bar standing).
5. Run the cart test too if you like (the row, A to turn it off, A again): `CART 16 MB: PAGES 2-7 OK`. It writes into
   the pages, so the next act start reloads what it overwrote (the engine's copies).
6. Anything odd: line 10's code if the text shows (5CB1 looking up, 5CB2 an engine copy, 5CB3 a page not usable, 5CB4
   a preload), or a photo.
