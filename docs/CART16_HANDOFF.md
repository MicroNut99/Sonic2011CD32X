# Hand-off: a bigger RAM cart (8 or 16 MB) with bank switching – faster loading, time travel from RAM

Written 17 Oct 2026, after R4D; the cart's spec and the CART TEST added 19 Oct (R4E); the 16 MB game mode (R4F, `BIG CART`) the same day; R4F's pairs replaced by the preload (R4G) the same day. For the next AI helping with this. The person you work with is designing a new RAM cart
for this port (Sonic CD 2011, RSDKv3, on Sega CD + 32X + RAM cart). They own the hardware and burn the discs; you build
and check everything before a disc is burned.

## The goal (the user's words, condensed)

- "The game needs to be able to load present, past and future data for a single level." Time travel should not wait
  for the CD.
- "There is no reason why the music cannot be delayed and/or faded out and back in to load data as a strategy."
- 8 MB or 16 MB? With the cart's 2 MB pages (below): 16 MB holds all four periods of the current act (a page each)
  and three more pages; 8 MB holds three of the four. Recommended 16 MB. Both need bank switching: the SH2 sees only
  4 MB of cart. **The bank switching is "the next big hurdle".** (As built in R4F - pairs of pages, no new scene
  images - 16 MB holds an act's four periods, 8 MB two: below.)
- 19 Oct: the user is not testing on the console any more unless it is needed (a 30-year-old single-speed drive, lost
  hardware): build as far as the emulator and the PC model can prove, one disc when it is worth it.

## Read first

| File | What |
|---|---|
| `README.md` | the project, the rounds, where things are |
| `docs/R4D_NOTES.md` | the latest round (the second SH2, stage sound banks, underwater colours, Tails) - its build commands and checks are the current way of working |
| `SONICCD_HANDOFF.md` | the full log of rounds S1-S7 (the boot, the cart, the loader) |
| `KOBO_CD32X_BOOT_PROCESS.md`, `docs/KOBO_COMMAND_LEDGER.md` | the boot and the 68000 commands (MD_CMD 1-35) |
| `tools/sh2emu/README.md` | the SH2/32X emulator every disc is checked in before burning |
| `cd32x/sub_main.c` | the Sub-CPU program: the SH2's commands (COMM0) - 38 open, 46 chunks into the cart, 45/54 music, 48 pad, 53 sound bank, 56-59 backup RAM, 68 the Genesis planes |
| `cd32x/kobo/hw_md.s` | the main 68000's listener (runs from 0xFF1000): its MD_CMDs and the per-frame plane record |
| `cd32x/sh2_engboot.c` | the SH2 boot loader (`SCDSH2.BIN`): the engine and the first scene into the cart |
| `cd32x/engine/plat_mars.cpp` | the engine's 32X platform: scene loading (`plat_stage_loading`, `sd_load_list`), the cart addresses |

## The user's standing rules (keep them)

- "Once you have a working boot stack then do not touch it." The boot stack = `sub_main.c`, `hw_md.s`, `sh2_engboot.c`
  (and what they build: `APP.BIN`, `SCDSH2.BIN`). Change it only with the user's explicit go-ahead, and then behind an
  Options Menu switch that is off at power-on (off = the previous disc's behaviour), as R4D did.
- "If touching the boot stack means adding more cmds I am only willing to do so if that is true" - a new command
  number only when nothing existing can carry it (R4D reused command 53 with a bank number in COMM2).
- Fewer discs and power cycles: everything is checked in sh2emu first; burn only on `VERDICT no problems found`.
- Makefiles are the user's (`cd32x/Makefile`, `host/Makefile`): never deliver them.
- Nothing made from the game's data is committed or delivered (`engine/ScriptNative.inc/.ops`, `CDTracks.inc`,
  `SFXBank.inc`, WAVs, `KOBOSFX.BIN`, `SFX*.BIN`, `*.MDG`, `*.IMG`, `IMAGE.RAW`, `game_data*/`). The ROMs in `Sega.zip`
  are not used.
- Commits stay local (never push). Command blocks for the user start with `cd` and use full `/mnt/s/...` paths. A
  pre-burn note with every delivery: the last good disc, the changed files with md5s, the boot path, what to expect.
- Don't make things up: what is not measured or not known is said so.

## How the cart is used today (4 MB, MicroNut99's RAM cart)

Who sees it:

- **The SH2s** read it at `0x02000000-0x023FFFFF` (cached) and `0x22000000-...` (cache-through) - the 32X's 4 MB cart
  window. **They never write it** (the code treats the cart as read-only for the SH2: `scd_pm.h`, "the cart, which the
  SH2 cannot write"). The engine's code runs from the cart (0x02000000+) and from SDRAM (`.sdtext`).
- **The main 68000** writes it at `0x400000 + offset` (Sega CD mode) with the 32X's RV bit set (`MD_CMD_CHILLY_RV_MEMCPY`,
  33) - while RV is set the SH2 must not touch the cart (sh2emu checks this: `rv_active`). It reads the act file
  through the 32X bank window `0x900000` (bank register `0xA15104`).
- **The Sub-CPU** never touches the cart: it reads the CD into its Word RAM bank and asks the 68000 to copy.

The map (offsets in the cart):

| Offset | Size | What | Code |
|---|---|---|---|
| `0x000000` | 512 KB | the engine (`SONICCD.PAK`, about 392 KB used): its code runs from here | `sh2_engboot.c` ENGINE_MAX |
| `0x080000` | 2.5 MB | the current scene's image (`L<list>S<pos>.IMG`): sprite sheets, tiles, the scene's data pack, native scripts, its chunks' CRCs. Acts use about 1.1 MB, the menus up to 2.3 MB | `CART_STAGE` (plat_mars.cpp), SCENE_MAX |
| `0x300000` | 512 KB | `SCDDATA.PAK` (the start scene's pack, for the boot) and the loading picture after it | `CART_PACK` |
| `0x380000` | up to 448 KB | the act's Genesis file (`G<list>S<pos>.MDG`: cells, colours, the plane records' data) for the 68000 | `SGB_CART_OFFSET`, `SGB_SLOT` 28 |

How a scene loads (`plat_stage_loading` → `sd_load_list`): command 38 opens the image; its chunk CRC table is compared
with what the cart holds; only the 32 KB chunks that differ are loaded, in runs of up to 4 chunks per command 46 (one
CD read into the Sub-CPU's 128 KB Word RAM bank, then the 68000 copies into the cart; the SH2 checks a checksum, and
reloads a bad run chunk by chunk the verified way). Then the act file the same way at `0x380000`, then command 68
turns the Genesis planes on. CD music (command 45, a whole track; 54 stops it) and CD data reads cannot overlap:
reading data stops the music.

Time travel in the game is a normal scene change: the act's periods are separate stage folders next to each other in
the regular stage list (`R11A` present, `R11B` past, `R11C` good future, `R11D` bad future = positions 0-3; act 2 =
4-7; act 3 has only C and D = 8-9; each zone 10 positions: zone 1 at 0, 3 at 10, 4 at 20 ... 8 at 60). Tails's images
are the same positions + 128 (`L1S128.IMG` ...).

## Measured (the R4D disc data, 32 KB chunks, duplicates removed by content)

| What | Size |
|---|---|
| the whole game (165 scene images + 142 act files, deduplicated) | about 126 MB (74 + 51.5) |
| one act period as loaded today (image + act file) | 1.42-1.50 MB |
| one act, all periods (acts 1 and 2: 4 periods) | 4.13-4.77 MB |
| act 3, both periods | 2.35-2.70 MB |
| title + main menu | 2.19 MB (+ time attack: 3.19 MB) |
| special stage 1 | 1.38 MB |
| an act change today (the chunks that differ + the act file) | about 1.1-1.3 MB; title → menu 1.38 MB; act → special stage 1.66 MB |
| Palmtree Panic 1, all 4 periods, compressed | zlib 1.17 MB; a simple LZSS (4 KB window, 32 KB blocks) 1.66 MB (raw 4.63 MB) |

The drive is single speed, about 150 KB/s plus seeks: today's act change is at least 7-9 s of reading. sh2emu
shortens CD reads (`sh2emu.c`, the Sub-CPU model's latencies), so its load times are not the console's. The measurement scripts were throwaway
(md5 of each 32 KB chunk over `L1Snnn.IMG` + `G1Snnn.MDG` of the act's positions): rewrite them if you need them.

File sizes on the R4D disc: act scene images 1.04-1.07 MB (largest 1,127,120 bytes), act files (`.MDG`) 0.35-0.43 MB
(largest 454,260), special stage images 1.57-1.82 MB, the title's and menus' images 1.62-2.20 MB (largest 2,306,768).
An act period (image + act file) fits a 2 MB page; a menu's image does not.

## The cart (the user's spec, 19 Oct 2026)

- **Two windows of 2 MB:** window 0 = cart `0x000000-0x1FFFFF` (the 68000 in Sega CD mode: `0x400000-0x5FFFFF`; the
  SH2: `0x02000000-0x021FFFFF`), window 1 = cart `0x200000-0x3FFFFF` (`0x600000-0x7FFFFF`; `0x02200000-0x023FFFFF`).
- **Each shows one of 8 pages of 2 MB.** Select with a word or upper-byte write, the page number in D8-D10, to
  `0xA130F0` (window 0) or `0xA130F2` (window 1). Power-on and /RESET: window 0 = page 0, window 1 = page 1 - today's
  linear 4 MB, so every older disc runs unchanged.
- Pages 0-3 are U1, 4-7 are U2; **an 8 MB build aliases 4-7 onto 0-3.**
- With the 32X adapter on: **write the registers with RV = 1 and interrupts off, and purge the SH2 caches after a
  switch.**

What follows from it:

1. **Only the 68000 writes the registers** (the SH2 cannot reach `0xA130xx`). The 68000 listener already has the right
   routine: MD command 26 `SAFE_CART_XFER` (interrupts off, RV = 1, byte copy, RV = 0) - a one-byte copy of the page
   number to `0xA130F0`/`F2` is the upper-byte write. **No `hw_md.s` change.**
2. **The Sub-CPU sends it** when the SH2 asks: R4E added it to command 46 - `COMM2 = 0xF800 | window << 8 | page`
   (a fast run's COMM2 is at most `0x03FF`), reply COMM12 = 0. No new command number. (`cd32x/sub_main.c`.)
3. **Window 0 switches only to a page holding the same engine**: the engine's code runs from it (cart
   `0x000000-0x07FFFF`). R4E's test never switches it; R4F's pairs do, each pair's page 2k with its own copy of the
   engine, compared byte for byte with the running one before the first switch (sh2emu: `CART-W0`).
4. **While RV = 1 the SH2 must not touch the cart** - code or data. The engine's command waits (`sd_cmd`, in SDRAM)
   already work that way for chunk loads. After the switch the SH2 purges its cache (`*(volatile unsigned char
   *)0xFFFFFE92 = 0x11`); the second SH2 purges before every job anyway.
5. **The SH2 does not write the cart** (assumed for the new cart too - the user gave no preference): loading into a
   page = map it into window 1, then the usual Sub-CPU chunk loads (command 46) into `0x200000 + offset`.

**The cart itself** (MicroNut99/16MB-PCB-CD32X, rev 0.5, not built yet): two CY62187 SRAMs (8 MB each), a GreenPAK
mapper, **battery backup** (CR2450, TPS3613: the SRAMs are deselected while the power changes): what the pages hold
stays through power-off. Its README: "Detecting the build" (8 or 16 MB) and the page registers as above.

**The design built in R4G (`cd32x/engine/plat_mars.cpp`, "THE PRELOAD") - what the game does now:** pages 0/1 as the
4 MB game (the boot loader's engine and title, the menus, special stages, title demos, the data pack, one act file
slot at 0x380000); **act pages 2-6**: each its own copy of the engine (byte for byte the running one's, so window 0
switches under the running code) and one act scene's image at 0x080000, used in place; page 7: the act files of act
pages 2-5 (512 KB slots at window-1 0x200000/0x280000/0x300000/0x380000; page 6's in page 1's slot). At an act's start
(not a title demo: `Options.AttractMode`) the act's other periods and the next act's start (act 2's present, act 3's
bad future, the next zone's act 1) go into the other act pages, the most needed first (present, past, next act,
bad future, good future: the last into page 6, whose act file shares page 1's slot with the demos, the credits and
the dev menu's font); warps and restarts are page switches. After a
power-on the first act scene looks every act page and slot up in the preload table at the end of `SCDDATA.PAK`
(`tools/build_preload.py`: number, length, the loader's checksum, a header fingerprint - the boot loader brings it in
at every power-on) and checks it whole; a page loaded halfway when the power went off is loaded again, in full (its
header's chunk list is not trusted). Found when the title starts, no cart RAM written (only the window 1 page
register: to page 0 and page 4, compared with window 0 at the engine's code and at the scene image's header, back to
page 1). **The boot stack against R4D (the last disc proven on the console):** `sub_main.c` has the two cases R4E/R4F
added inside the existing command 46 (`0xF8nn` page register write through the 68000's command 26, `0xF0nn` the
opened file's slot) - needed, because none of R4D's commands lets the SH2 choose where the 68000 writes, so R4D can
never switch a page; the user accepted them on 19 Oct after asking for no boot-stack edits (say this plainly: an
earlier claim that R4G needed "no boot-stack edit" compared with R4F, not with R4D, and misled the user). Nothing else:
`hw_md.s` and `sh2_engboot.c` are R4D's. 8 MB: one act page (2, its act file in page 3) for the act's first
scene; the act's other periods load in page 0 as on the 4 MB cart. The rest of this section is R4F's design, kept for the record.

**The design built in R4F (`BIG CART`, `cd32x/engine/plat_mars.cpp`, "THE BIG CART" - replaced by R4G):** the first idea - acts
linked for window 1 - needs a second set of scene images (the native scripts are linked to `0x02080000 + offset`) and
moves `CART_STAGE`. R4F does without: the cart is used as **pairs of pages**, pair k = page 2k in window 0 + page
2k + 1 in window 1, each pair laid out exactly as today's 4 MB cart (the engine at `0x000000`, the scene image at
`0x080000`, `SCDDATA.PAK` with the loading picture at `0x300000`, the act file at `0x380000`). Pair 0 = pages 0/1 = the
cart at power-on. 16 MB = four pairs, 8 MB = two.
- A scene a pair holds: both windows switched to it (window 1 first, then window 0 - the same engine code in both
  pages, so the SH2 runs on; its cache purged after each). No CD read; the screen stays black for the change.
- A new act scene: into a pair holding none of the same act's periods (the least recently used); if every other pair
  holds one (8 MB), the least recently used of them - never the pair in use, so the period being left stays in.
  An act = (player, zone, act): Sonic's and Tails's images are different scenes.
- Other scenes (the title, the menus, the special stages, the dev menu's font) use the pair in use.
- A pair used the first time: `SONICCD.PAK` (file 21) into page 2k and `SCDDATA.PAK` (22) into page 2k + 1, each
  through window 1 (command 38, then 46 `0xF000 | slot`: slot 16 = `0x200000`, 24 = `0x300000`, then the usual
  chunk runs); the engine copy compared with window 0 before the pair is used. About 520 KB more to read the first
  time (396 KB + 127 KB: about 3.5 s at 150 KB/s, plus seeks - computed, not measured), never again until power-off.
- Each pair keeps its own scene number, act file number and size; the chunk-diff loader works per pair unchanged
  (it compares with the image in the pair's own page).
- The Options Menu's last row, `BIG CART`: A runs the cart test (R4E's) and, when it passes, turns the mode on (16 MB
  or 8 MB); A again turns it off (the next scene load goes back to pages 0/1). Off at power-on: the game is R4E's.

## Engine-side work

**Done in R4E:** the Sub-CPU's page switch (above), sh2emu's model of the mapper (`-cart 4|8|16`, `-cartbad P`: a
page that keeps nothing; a cache line read after a switch without a purge shows as `STALE-READ`), and the Options
Menu's **CART TEST** (`plat_cart_test`, `cd32x/engine/plat_mars.cpp`): read-only first (window 1 showing page 0 = the
switch works; page 4 = page 0 = an 8 MB cart; a page showing page 0 is never written), then a 32 KB pattern into each
of pages 2-7 (2-3) from the test file `KOBOGFX.BIN` (`tools/make_cart_test.py`; file 16, which the Sub-CPU puts at
`0x200000`), read back by the Sub-CPU through the 68000 and by the SH2 through its cache, and no page showing another's.
The game's pages 0 and 1 are never written. The result in the Options Menu's two note rows. This is the first thing to
run on the new cart.

**Done in R4F:** the pairs (above) - `pair_for`, `pair_prepare`, `pair_use`, `scene_resident` in `plat_mars.cpp`;
`BIG CART` in `engine/Debug.cpp`; the Sub-CPU's slot command (command 46, COMM2 `0xF000 | slot`: `cur_img` = slot);
sh2emu's slot command and its `CART-W0` check. Checked in sh2emu only (docs/R4F_NOTES.md).

**Done in R4G:** the preload (above): `big_prepare`, `big_preload`, `big_identify`, `big_detect` in `plat_mars.cpp`;
`tools/build_preload.py`; sh2emu `-cartkeep FILE` (the cart's 16 MB kept between runs: the battery). The `CART TEST`
tells an alias of page 0 by the engine and the scene image together (an act page has the engine's copy), and does
not run while page 0 holds an act scene (an act page with the same scene would look like page 0). Loads into an act
page or an act file slot refuse a file bigger than the room (none is today).

**Still to do:**

- More prefetch in the moments the game already has: the results tally (a special stage when the big ring is
  possible), death and restart, pause, the warp itself (each period has its own music track, so the track changes
  there anyway). R4G loads at an act's start; the SH2 cannot run the game while the 68000 writes the cart (RV = 1), so
  a load is always a pause in play. Fading the music out for a data burst needs the Sub-CPU to
  resume a track at a position: command 45 plays whole tracks only today - a Sub-CPU change (ask first). Each switch
  between music and data costs a seek: a few long bursts, not many short ones.
- Compression (`tools/build_game_data.py`, `tools/build_mdg.sh` write the files): halves to thirds the bytes read; the
  SH2 does not write the cart, so the unpacker would be the Sub-CPU's (a boot-stack change - ask first).
- The engine copy from the CD costs about 2.6 s per new act page, once (396 KB at 150 KB/s; the battery keeps it): the
  68000 could copy page 0's engine instead (`CHILLY_RV_MEMCPY` from `0x400000` to `0x600000`, in 32 KB pieces) - a
  Sub-CPU change, ask first.
- The boot loader still reads the engine, the data pack and the title at every power-on: with the battery it could
  check them instead (`sh2_engboot.c`, the boot stack - the user said "not now").
- The PC model (`host/plat_host.cpp`) has no cart: `BIG CART` stays off there.

## How things are checked here (copy the way R4D was done)

- The build: `docs/R4D_NOTES.md` "Build" (WSL, the toolchain at `/opt/toolchains/sega`, the game's `Data.rsdk`).
- sh2emu: power-on to Palmtree Panic 1 with `-pad`, `-bram`, `-shot`; `-set NAME=V` sets an engine variable (a switch);
  `VERDICT no problems found` before any burn.
- The 68000 listener was tested in a 68000 emulator (Musashi) with a model of the VDP and the interrupts - that harness
  lived in a scratch folder and is **not in the repo**; rebuild one if `hw_md.s` changes (gas with
  `--register-prefix-optional -I cd32x/kobo`, the `.text` copied to 0xFF1000).
- A delivery = the changed files since the last round in a zip + md5s + the expected md5s of every built file, checked
  by building the previous round's tree + the zip + the user's Makefiles from scratch.

## Questions for the user

Answered (19 Oct): the mapper (above); the power-on mapping (today's 4 MB); the first test on the cart is the CART
TEST row, before the game uses the pages. Open: can the SH2 write the new cart (no preference given - assume not);
8 or 16 MB in the end; does the 4 MB v01 cart use `/TIME` (B31) for anything - R4G writes `0xA130F2` twice at every
power-on (the 16 MB cart found), which R4D never did (a Fable review of R4G, 19 Oct, raised it). **On the console, 19 Oct (the owner's photo):** a disc with the new `sub_main.c` (R4F or the first R4G
build) booted on the 4 MB cart, and the cart test's page commands were answered: `CART TEST: NO PAGE MAPPER` / `(A 4
MB CART) - NOTHING WRITTEN`, nothing hung. **Then the slot case (19 Oct, the second R4G zip, the owner's photo):**
`CART TEST: NO PAGE MAPPER (4 MB)` / `SLOT 21 LOAD OK (SUB-CPU AND SH2)` (run from the main menu: slot 21, cart
`0x2A0000`). Both new Sub-CPU cases have run on the console; a real page switch needs the board.
