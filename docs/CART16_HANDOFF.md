# Hand-off: a bigger RAM cart (8 or 16 MB) with bank switching – faster loading, time travel from RAM

Written 17 Oct 2026, after R4D. For the next AI helping with this. The person you work with is designing a new RAM cart
for this port (Sonic CD 2011, RSDKv3, on Sega CD + 32X + RAM cart). They own the hardware and burn the discs; you build
and check everything before a disc is burned.

## The goal (the user's words, condensed)

- "The game needs to be able to load present, past and future data for a single level." Time travel should not wait
  for the CD.
- "There is no reason why the music cannot be delayed and/or faded out and back in to load data as a strategy."
- 8 MB or 16 MB? Answer given: 8 MB holds every period of the current act; 16 MB also holds the next act, a special
  stage and the menus (numbers below). Recommended 16 MB if the cost is similar. Both need bank switching: the SH2
  sees only 4 MB of cart. **The bank switching is "the next big hurdle".**

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

Budget, everything uncompressed:

| Kept in the cart | 8 MB | 16 MB |
|---|---|---|
| engine | 0.5 | 0.5 |
| current act, all periods (worst) | 4.8 | 4.8 |
| next act, all periods | - (about 1.7 compressed) | 4.8 |
| a special stage | - | 1.4-1.7 |
| title + main menu | 2.2 if nothing else | 2.2 |
| total | 5.3 + about 2.7 spare | about 14-15 |

## The hurdles - what a design must answer

1. **The SH2's window is 4 MB.** More RAM is reachable only by switching what the window shows (or by copying into
   it). The engine's code (cart `0x000000-0x07FFFF`) must stay mapped while it runs - a switch of that part would have
   to run from SDRAM.
2. **The SH2 caches cart reads.** After a switch both SH2s must purge their caches (CCR purge, as the second SH2 does
   before each job: `secondary()` in `sh2_engboot.c`, `SC_CCR = 0x11`), or they read the old bank's lines.
3. **The SH2 does not write the cart today.** So unpacking compressed data into the cart, or copying between banks,
   falls to the 68000 or the Sub-CPU (boot stack) - **unless the new cart lets the SH2 write**. That one design choice
   decides where the unpacker and the copier live. Worth asking the user first.
4. **The 68000 reads the act file during its vertical blank** (the plane record, cells from `0x900000`): a switch of the
   act file's part must not happen mid-read. The record already goes from the SH2 to the 68000 every frame
   (`md/sgb.h`): a bank field in it would need no new command (but it is a `hw_md.s` change).
5. **Who writes the bank register, and where it is** - the cart's own address space (the 68000 sees it at 0x400000+),
   the Genesis `/TIME` area (`0xA130xx`, where Sega's 512 KB-page mapper lives on 4 MB+ Genesis carts), or something
   else. **Not verified here:** whether the 32X passes such writes to the cart and whether the SH2's window follows a
   cart-side mapper. Ask the user (they design the cart) and test on hardware before relying on it.
6. **Power-on must look like today's linear 4 MB**, so the existing boot (and an R4D disc) works unchanged.

A layout that fits today's map: 512 KB pages. The scene area `0x080000-0x2FFFFF` is exactly pages 1-5, the data pack
page 6, the act file page 7; page 0 (the engine) fixed. An act period = 3 pages of image (acts use about 1.1 MB) + 1
page of act file; time travel = pointing pages 1-3 and 7 at the period's pages (no copy, then the cache purge). Pages
waste the space a period's image does not fill and cannot share chunks between periods (4 periods ≈ 8 MB of pages vs
4.8 MB deduplicated) - with 16 MB that is still room for the current act and most of the next. A copy design (a
4 MB window as today + a stash the 68000 copies from) shares chunks but costs a copy at each switch (not measured).

## Engine-side work (none started)

- `cd32x/engine/plat_mars.cpp`: a table of what each bank holds (scene, act file, CRCs); `plat_stage_loading` switches
  banks for a resident period instead of loading; the chunk-diff loader writes into the bank the next period will use.
- Prefetch in the moments the game already has: the act's title card (load all periods), the results tally (the next
  act, a special stage when the big ring is possible), death and restart, pause, the warp itself (each period has its
  own music track, so the track changes there anyway). Fading the music out for a data burst needs the Sub-CPU to
  resume a track at a position: command 45 plays whole tracks only today - a Sub-CPU change (ask first). Each switch
  between music and data costs a seek: a few long bursts, not many short ones.
- Compression (`tools/build_game_data.py`, `tools/build_mdg.sh` write the files): halves to thirds the bytes read; the
  unpacker's CPU depends on hurdle 3.
- `tools/sh2emu/sh2emu.c` must model the mapper (the window, the registers, a read of an unmapped page, a missing cache
  purge, RV) **before** any disc - the rule is sh2emu first. The PC model (`host/plat_host.cpp`) loads the same way and
  should follow.
- Keep it behind an Options Menu switch (`engine/Debug.cpp`, the R4D switches show how), off at power-on, until the
  console has shown it works.

## How things are checked here (copy the way R4D was done)

- The build: `docs/R4D_NOTES.md` "Build" (WSL, the toolchain at `/opt/toolchains/sega`, the game's `Data.rsdk`).
- sh2emu: power-on to Palmtree Panic 1 with `-pad`, `-bram`, `-shot`; `-set NAME=V` sets an engine variable (a switch);
  `VERDICT no problems found` before any burn.
- The 68000 listener was tested in a 68000 emulator (Musashi) with a model of the VDP and the interrupts - that harness
  lived in a scratch folder and is **not in the repo**; rebuild one if `hw_md.s` changes (gas with
  `--register-prefix-optional -I cd32x/kobo`, the `.text` copied to 0xFF1000).
- A delivery = the changed files since the last round in a zip + md5s + the expected md5s of every built file, checked
  by building the previous round's tree + the zip + the user's Makefiles from scratch.

## Questions for the user before designing the software side

1. 8 or 16 MB, and the page size (512 KB pages would match today's map).
2. Where the bank registers are and which CPU writes them (68000 only? the SH2?).
3. Can the SH2 write the new cart?
4. The power-on mapping (should be today's linear 4 MB).
5. Will the first cart be tested with a small hardware test disc first (a few pages written and read back by both
   CPUs) before the game uses it? That would be the cheapest first burn.
