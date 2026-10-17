# sh2emu: check a build on the PC before burning it

`sh2emu` runs a build's SH2 side on the PC, exactly as it would go on the disc:

1. It boots the build's own SH2 program, `SCDSH2.BIN` (Kobo's crt0 and the loader `cd32x/sh2_engboot.c`).
2. It loads the disc files through the same command 38 / 46 handshake the Sub-CPU answers on the console.
3. It starts the engine (`SONICCD.PAK`) and runs it frame by frame, with the 60 Hz vertical-blank interrupt, Kobo's
   interrupt handlers and the PC sampler.

While it runs, it watches every memory access for the things that hang or corrupt the console, and at the end it says
whether the build is safe to burn.

When the Genesis planes are on, it also plays the 68000's part: it takes each plane record the SH2 hands over, as
`sgb_poll` does, and writes it into a model of the Genesis VDP in vertical blank, as `sgb_vblank` does. With `-shot`
it saves the Genesis picture - the two planes and, from R3, the sprites - as a `.bmp` file, so you can see what the
console should show before you burn.

Its timing matches the console (measured on R1S): the game speed it reports is the speed you will see. With
`-profile` it shows where the SH2's time goes (section 4).

**R4D:** it runs the 32X's **second SH2** too, from its own boot-table entry (Kobo's crt0, then the loader's
`secondary()`): both SH2s on the 32X's one bus (an access waits for the other's), each with its own registers, cache,
divider and interrupt mask. The engine's jobs for it (the Options Menu's `2ND SH2` switch) come through the loader's
mailbox (SDRAM `0x06001F40`); while it waits for one it is not stepped (it costs no time). `-noslave` leaves it out
(as sh2emu before R4D). It plays the Sub-CPU's command 53 with a bank number (`SFX<n>.BIN`, the `STAGE SFX` switch)
and the 68000's underwater colours (the records' water trailer: the water colours below the water line in `-shot`
pictures, the normal ones above).

## 1. Build it (once)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
```

This uses the PC's own gcc, not the Sega toolchain. Build it again only when `sh2emu.c` changes.

## 2. Run it on every build, before burning

Build the disc as usual (`make ... ENGINE=1`). That leaves everything sh2emu needs in `cd32x/build_engine`. Then, for
R1-R3 builds (**R4 builds: the command with `-pad` below** - without a controller an R4 build stays in its menu, and
the summary says `stage loaded no`: nothing of the game was checked):

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
tail -25 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
```

It runs 25 seconds of console time (1500 frames) in about 15 seconds. The full log is in `sh2emu_log.txt`. Send me
that file whenever the verdict is not clean.

To see the Genesis picture, add `-shot FRAME FILE.bmp` (up to 64 of them) and open the `.bmp` files in Windows:

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -shot 700 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_700.bmp -shot 1300 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/shot_1300.bmp > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
tail -25 /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
```

The planes come on at about frame 590. Frame 700 is the start of Palmtree Panic 1. The scripted pad starts moving
about 4 seconds later, so frame 1300 is further into the act.

R3 builds: Sonic's act ends at about frame 5400 and the results end at about 6080; then the SH2 looks for the next
act (`L1S004.IMG NOT FOUND`), loads Tails's files and starts the act again as Tails (title card at about frame 6300).
`-f 7000` covers the switch; after Tails's results (about frame 14090) Sonic's act comes back (`-f 16500`).

**R4 builds boot into the Level Select menu** (at about frame 270), and nothing plays until a button is pressed. Give
the run a controller with `-pad`: this one switches DEMO PAD on (Down, Down, A), goes back up to START GAME (Up, Up)
and presses Start - Palmtree Panic 1 loads at about frame 770 and the scripted pad plays it. **This is the R4 check
before burning**; the second line prints the summary (`stage loaded yes (STATE 5100 ...)`, `VERDICT`):

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 5000 -pad "600:D,610:-,620:D,630:-,640:A,650:-,660:U,670:-,680:U,690:-,700:S,710:-" > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
```

Add `-profile 1000 5000 -syms /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/game_data/native/scene_1_0.syms`
for where the time goes (section 4): the profile is printed after the summary, so read it with the `sed` line above or
in the file, not with `tail`.

A stage from `STAGE SELECT`: `D`, `D`, `A` (DEMO PAD on), `U` (to `STAGE SELECT`), `A` (the player list), `D` for Tails,
`A` (the stage lists), `D` (`REGULAR`), `A`, then one `D` per act down the list - held 4 frames, released 6 (held
longer than 8 frames, the menu repeats it) - and `S`. Each key as `FRAME:KEY,FRAME:-`, e.g. Collision Chaos 2 present
(the 15th act, 14 `D`s) as Sonic:
`-f 3000 -pad "600:D,610:-,620:D,630:-,640:A,650:-,660:U,670:-,680:A,690:-,700:A,710:-,720:D,730:-,740:A,750:-,760:D,764:-,770:D,774:-,780:D,784:-,790:D,794:-,800:D,804:-,810:D,814:-,820:D,824:-,830:D,834:-,840:D,844:-,850:D,854:-,860:D,864:-,870:D,874:-,880:D,884:-,890:D,894:-,900:S,910:-"`

**R4C builds boot into the game's own title screen** (the scene `L0S000.IMG`, at about frame 370; the main menu at
about 1570). This pad presses Start on the title, then A on `START GAME`, on the first save file and on Sonic -
Palmtree Panic 1 loads at about frame 3830 and the act starts (nothing moves Sonic: the act's own start, the HUD's
clock running). **This is the R4C check before burning**; `-bram` gives the run a backup RAM file (made if it is
not there): `backup RAM ... 1 saves` in the summary is the new game's save (`SONICCD32X_`, 512 bytes):

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 6000 -pad "1400:S,1410:-,2300:A,2310:-,2650:A,2660:-,3300:A,3310:-,3600:A,3610:-" -bram /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_bram.bin > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
sed -n '/= SUMMARY =/,/^VERDICT/p' /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
```

In the scenes the 32X draws itself (R4C's picture mode: the title, the menus, time attack, the special stages)
`-shot` saves the 32X picture; in the acts, the Genesis picture as before. The Options Menu of R4A / R4B is A + B + C
+ Start from anywhere: e.g. `1300:ABCS,1320:-` on the title (it shows at about frame 1400), then the R4B menu's keys
as above (`D`, `A`, `D` for the `SPECIAL` list, `A`, `S`: the first special stage at about frame 1720).

The build folder must hold:

- `SCDSH2.BIN`, `sh2.map`, `engine.map` and `engine_symbols.txt`;
- `disc/`, with `SONICCD.PAK`, `SCDDATA.PAK`, the `L*.IMG` files and the `G*.MDG` files.

A normal `make ENGINE=1` leaves all of these.

## 3. Read the result

The last lines of the log are the summary. This is the R1Q build, the disc that froze on the console:

```
==================== SUMMARY ====================
build folder      cd32x/build_engine
run               900 frames (15.0 s of console time)
engine started    yes (frame 463)
stage set-up 5C16 yes (frame 467)
stage loaded      yes (STATE 5100, frame 592)
planes on (CMD 68) yes (frame 596)
line 10 now       STATE:1 CKPT:0038 COMM4:0097
PROBLEMS (can hang or corrupt the console):
  R-UNMAPPED  x3078     first at frame 502, 02015D8C <ProcessScript(int, int, unsigned char)+0x44>
  R-AREA      x7695     first at frame 502, 02015D8C <ProcessScript(int, int, unsigned char)+0x44>
warnings:
  R-UNDEF     x67714    first at frame 464, 02020CCA <scd_fopen(char const*, char const*)+0xA>
information:
  W-COMM      x152      first at frame 0, 060002A8
  ...
VERDICT           PROBLEMS FOUND - do not burn yet
```

| Line | What it means |
|---|---|
| `engine started` | the loader loaded the engine, the data pack and the first scene, and jumped to the engine |
| `stage set-up 5C16` | the engine began loading its first stage (line 10 `5C16` on the console) |
| `stage loaded` | the stage finished loading (`5Lnn`: list L, scene nn; `5100` = Palmtree Panic 1) |
| `planes on (CMD 68)` | the SH2 asked for the Genesis planes and the Sub-CPU accepted (R4: the first is the Level Select menu's) |
| `game speed` | engine frames per second of play, after the stage loaded. The game is made for 60; R1S: 26-29 |
| `plane records` | records the SH2 sent, the 68000 took, and the VDP got. `INCLUDING A JUNK RECORD` = COMM4 held a value when the planes came on, and the 68000 took it as a record: a dot in every blank cell (R1G, R1R). Listed as the problem `JUNK-RECORD` |
| `record contents` | a CRC of the first 600 records (option `-reccrc N`). It depends only on the pictures, not on the speed: a speed change that keeps this CRC keeps every picture the same |
| `controller` | (R4) the pad requests (command 48) the Sub-CPU answered - one a frame |
| `audio` | (R4) sound effects played (command 48's upper byte; the first 40 are listed as `SUB: sound effect N`) and CD audio commands (45 play, 54 stop: `SUB: CMD 45 play track N`); (R4D) sound banks loaded (command 53: `SUB: CMD 53 load SFX06.BIN -> 28 effects`). An effect number the bank in the PCM chip does not have is the warning `A-SFX` |
| `second SH2` | (R4D) the jobs it ran for the engine (the `2ND SH2` switch), its instructions outside the loader, the cycles it waited for the bus, its stack's lowest point. "waits in a loop to itself": a loader before R4D (its `secondary()` does nothing) |
| `pictures` | (R4D) frame buffer swaps - in the 32X picture mode one a picture: how many in the run's last 10 seconds |
| `underwater` | (R4D) frames with a water line: the 68000's H-int writes the water colours from that line down (the `UNDERWATER` switch) |
| `frame-buffer FIFO` | (only when it happens) words the SH2 wrote to the frame buffer that were still in the 32X's 4-word write FIFO when it gave the frame buffer to the 68000. The 32X does not guarantee them |
| `line 10 now` | what line 10 would show at the end |
| `PROBLEMS` | **do not burn.** Each kind of problem is listed with how many times it happened and where it happened first (the function and the offset into it); R4: and the places with the most, under it |
| `warnings` | reads of memory nothing has written; worth a look, not a reason to stop. About 116 in `scd_fopen` / `LoadFile` at start-up are normal (local variables read before they are set, in code the console has always run); millions are not (R4: the foreground's line scroll entries) |
| `information` | normal register traffic, and the engine's expected writes into the loader's area (`W-SAMPLING`: the sampler switch, `W-VECTOR`: its exception handlers, `W-BOOTREC` (R3): the number of the scene image now in the cart, after a scene change; `DIVU` (R4C): the 32X picture mode's divisions on the SH2's own divider, 39 cycles each), listed for reference |
| `VERDICT` | `no problems found` = safe to burn as far as the SH2 and the plane records go |

Emulated time is counted in frames: 60 frames are 1 second on the console.

The earlier part of the log lists, as they happen:

- the **STATE lines**, which are what line 10 shows on the console;
- the Sub-CPU's file opens;
- the **event lines**;
- (R4) `scene image: native scripts at A-B`: the scene image in the cart carries its scripts as native code (part 18)
  there - from then on that range is code, like the engine's;
- (R4) `ENGINE LOG: ...`: the engine's own notes (`plat_log`, which prints nothing on the console), e.g. `native scripts
  on (the engine's and the scene's)` when a scene's scripts run as native code;
- (R4) `peek (stage loaded) NAME = V`: option `-peek`.

This event line is from R1Q:

```
[f  502 c192862346] R-UNMAPPED PC=02015D8C <ProcessScript(int, int, unsigned char)+0x44> PR=0200E774 <ProcessStartupObjects()+0x68> R15=0603ED2C addr=2348B304 size=4 val=00000000
```

| Part | Meaning |
|---|---|
| `f  502` | frame 502 (60 per second) |
| `c192862346` | the SH2 cycle count |
| `R-UNMAPPED` | the kind of event (see the table below) |
| `PC=... <function+offset>` | the instruction that did it |
| `PR=... <function+offset>` | where that function was called from |
| `R15=` | the stack pointer |
| `addr= size= val=` | the address it touched, the size and the value |

Each kind of event is logged at most 4 times per instruction (option `-maxlog`). The summary still counts all of them.

| Event | What happened | On the console |
|---|---|---|
| `R-UNMAPPED`, `W-UNMAPPED`, `R-AREA`, `W-AREA`, `R-IO`, `W-IO`, `R-ROM`, `W-ROM` | a read or write at an address nothing answers on the 32X (reserved SH2 spaces, past the cart or past SDRAM) | the SH2 can hang: no more interrupts, line 10 freezes |
| `W-CARTCODE` | a write into the engine's own code or tables in the cart | corruption |
| `W-LOADER` | a write into the loader or the vector table | corruption |
| `W-GAP` | a write between the engine's RAM and its stack | corruption |
| `STACK`, `EXCEPTION`, `ILLEGAL`, `ADDRESS ERROR`, `EXEC OUTSIDE CODE` | the stack pointer left its area, or the SH2 crashed | crash |
| `STALE-READ`, `STALE-CODE`, `REG-CACHED` | the cache gave back older data than memory holds | wrong data |
| `FB-NOFM` | the frame buffer was used while the SH2 did not own it | bus conflict |
| `FB-FIFO-LOST` | words still in the frame buffer's write FIFO when the SH2 gave the frame buffer to the 68000 | lost or late record words |
| `CART-RV` | the cart was used while the 68000 held it | bus conflict |
| `SR-HIGH` | engine code masked the vertical-blank interrupt | line 10 freezes |
| `R-UNDEF` (warning) | a read of memory nothing has written (the frame buffer and SDRAM are not cleared at power-on) | random values |
| `*** LINE 10 FROZEN` | the sampler stopped, as on the console; the run stops there and prints the registers and the call stack | the console's death signature |

To find the source line for an address, look the function up in `cd32x/build_engine/engine.map`. Then add the offset
and disassemble around it with the toolchain's `objdump -d` on `engine.elf`.

## 4. Speed: where the time goes

`-profile A B` measures frames A to B (60 a second). Use a stretch of play, e.g. `-f 2600 -profile 1300 2500`. After
the summary it prints:

```
ENGINE FRAMES     568: busy cycles each - mean 594438 (1.55 vertical blanks), median 1.46, 90% 2.30, max 2.56; 0% fit in one vertical blank ...
  per engine frame, all frames vs the slowest tenth (57 frames), in vertical blanks:
     0.57   1.38  ProcessScript(int, int, unsigned char)
     0.29   0.29  DrawHLineScrollLayer(int)
  scripts per engine frame by object type and event, all frames vs the slowest tenth, in vertical blanks:
    0.120  0.909  type  66 3DRamp                   player
    0.234  0.310  type   1 PlayerObject             main
```

| Part | What it means |
|---|---|
| `ENGINE FRAMES` | the SH2's busy time for each engine frame (everything but waiting for the vertical blank), in vertical blanks (383,500 cycles). **60 frames a second needs every frame under 1.00** |
| all frames vs the slowest tenth | per function, its average time per frame, and its average in the slowest 10% of frames: what makes the slow frames slow |
| scripts by object type and event | the script time (`ProcessScript` and everything it calls) by object type and sub (`main`, `player` = player interaction, `draw`) |
| `by function, per frame` | per vertical blank: cycles, instructions, and the stall cycles inside them - waiting for the cart (code or data), SDRAM, the frame buffer. `in SDRAM` = the cycles if the function's code were in SDRAM |
| `cart code stalls by function, cumulative` | the functions whose code costs the most cart fetches, with their sizes added up: what to move into SDRAM first |
| `cart data stalls by 4 KB of the cart` | which cart data (engine tables, the stage image's parts by name) the SH2 waits for |
| the slowest engine frames | (R1W) the six slowest frames, the console frame each ended in, and the functions that took most of each |
| `code in SDRAM` | (R1X) each function in `.sdtext`: the cycles a frame it saves against the cart (its code cache lines x 76) and per byte - with the cart list above, what to swap in and out |

`-callers NAME` adds who calls NAME (e.g. `-callers __sdivsi3`: who divides). `-pcprof NAME` (up to 8) adds NAME's every
instruction: per address the count, the cycles and the stalls by kind per frame (`PCP` lines; join them with
`objdump -d` of `engine.elf` to read the hot loop). `-shots A B STEP PREFIX` saves a picture every STEP frames from A to
B (to compare with a video of the console, or two builds with each other).

Code in SDRAM (R1V, the engine's `.sdtext` section): sh2emu reads its place from `engine.map`, counts it as engine code,
and reports any write into it after it started running (`W-SDCODE`, a problem). The stack: the engine's 8 KB
(0603D400-0603F400, R1W) - below that `STACK` / `W-GAP`.

The bus timing comes from the 32X manual (section 1.12) and was measured against the console on R1S: a single cart read
waits 15 cycles a word, a cart cache line (8 words in a row) costs 87 cycles, an SDRAM line 11, a frame buffer read 8;
the 68000 copies a record word in 75 SH2 cycles and is busy in its vertical blank handler. Options `-cartwait`,
`-cartrd`, `-cartfill`, `-fbread`, `-k68word` change these.

## 5. Options

All options go after the build folder.

```
-f N           run N frames (default 1500 = 25 s)
-v             print every STATE change and every Sub-CPU chunk
-maxlog N      log lines per (event, instruction), default 4
-zero          power-on RAM = zeros (default: pseudo-random)
-seed N        another pseudo-random power-on pattern
-nocache       run without the SH2 cache model
-hook ADDR     print the registers each time the SH2 reaches ADDR (at most -hookmax N times, default 50)
-trace N COUNT print COUNT instructions, starting at instruction number N
-dumpsdram FRAME FILE   save the 256 KB SDRAM at that frame (for checking the engine's tables afterwards)
-shot FRAME FILE.bmp    save the Genesis picture (planes model) at that frame; up to 64
-shots A B STEP PREFIX  a picture every STEP frames from A to B: PREFIX_<frame>.bmp
-records N     print the first N plane records the 68000 model applies (default 3)
-pad SCRIPT    (R4) the controller: "FRAME:BUTTONS,..." - U D L R A B C S X Y Z (S = Start), "-" = none;
               FRAME:demo = the scripted pad (spin dash, run right) from that frame on
-set NAME=V    (R4) the engine variable NAME (32 bits) = V when the first stage set-up starts (5C16)
-pokeg F:I=V   (R4B) the game's global variable I (globalVariables[I], in GameConfig.bin's order: 25 Warp.Destination,
               26 Warp.Timer ...) = V at frame F, as an SH2 write; up to 256 - e.g. a time warp on demand
-peekevery N   (R4B) the -peek values every N frames too
-peek NAME[:N] (R4) print the engine variable NAME (N = 1, 2 or 4 bytes) at each stage load and at the end, e.g.
               -peek scdBlob (not 0: the scene's native scripts are in use) -peek stageListPosition
-syms FILE     (R4) more function names, `nm -n -C` format: a scene's native scripts are named in
               game_data/native/scene_<list>_<pos>.syms (tools/build_game_data.py --native); up to 8
-reccrc N      the record contents CRC over the first N records (default 600)
-profile A B   where the SH2's time goes between frames A and B (section 4)
-callers NAME  with -profile: who calls the function NAME, how often a frame
-pcprof NAME   with -profile: NAME's every instruction - count, cycles, stalls per address (up to 8 functions)
-frames A B    with -profile: every engine frame that ended in console frames A..B, with the functions that took it
-cartwait N    cart waits for a single access (default 15); -cartrd N / -cartfill N set the read / line costs directly
-fbread N      frame buffer read cost (default 8); -k68word N the 68000's cycles per record word (default 75)
-nofifo        no frame-buffer write FIFO model; -fbword N = SH2 cycles per FIFO word (default 5)
-novbistop     keep running after line 10 freezes
-bram FILE     (R4C) the backup RAM's SONICCD32X_ file (512 bytes): read by command 56, written by command 59, kept in
               FILE between runs (a power cycle with the save in place); no FILE yet: an empty backup RAM
-badrun N      (R4C) the Nth fast load run (0 = the first) answers a wrong checksum: the SH2 must load those chunks
               again one by one, the old way (the summary's "chunks one by one")
-oldsub        (R4C) the Sub-CPU as R4B's: no fast command 46 - the SH2 must load everything the old way
-noslave       (R4D) the second SH2 not run (sh2emu as before R4D)
-h             this list
```

Examples:

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
# 2 minutes of console time
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 7200 > /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/sh2emu_log.txt
# every call of ProcessScript (address from engine_symbols.txt): its arguments are r4, r5, r6
/mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/cd32x/build_engine -f 650 -hook 0x02015d48 | grep HOOK | head
```

## 6. Limits

- **Timing.** It is not cycle exact, but it matches the console where it was measured: R1Q's first sample came out as
  `8F34` / `9550` (console `8F33` / `9550`); R1S loads its stage in 2.5 s (console 2.5-3.0 s) and stays within 0.1 s of
  the console's video for 20 s of play. Up to 16 Oct 2026 it did not charge the cart for instruction fetches; profiles
  from before then undercount the cart.
- **After the first problem.** Reads where nothing answers return 0 here. On the console they can return anything,
  or hang the bus. So the first problem is what counts, not what the program does after it.
- **The 68000 and the VDP are models.** The model follows `sgb_poll` and `sgb_vblank` exactly. When the 68000 takes
  an id while the SH2 still holds the frame buffer, the console reads undefined data; the model then uses what the
  console copied on R1H (`REC 0004 0004 0006 ...`). The VDP model draws the two planes, their scroll, the colours and
  (R3) the sprites: the sprite table at `0xF800`, the links, 20 sprites and 320 dots a line, the flips and the
  priority bits (high before low; sprite, plane A, plane B), and (R4) the 32X layer of the credits screen (the
  256-colour mode through its line table; colours without bit 15 behind the Genesis picture). (R4B) The Sub-CPU's
  commands 51 (a file's chunk into the frame buffer the 68000 sees, from byte 0x200 - the loading picture) and 52 (the
  Genesis display on / off) are modelled: with the display off the pictures show the 32X layer alone, on black; the
  file number 0 opens `IMAGE.RAW`, as on the Sub-CPU. It has no window, no
  sprite masking and no shadow / highlight. The 68000's copy takes 75 SH2 cycles a word, and it does not take a record while its
  vertical blank handler runs (`-k68word`).
- **Loading speed.** The Sub-CPU answers at once. On the console a 32 KB chunk takes about a second, so the loading
  part of the timeline is much shorter here.

## 7. What it found

**R1Q.** On the console the SH2 died after the stage set-up started: line 10 showed `8F33` / `9550`, then nothing.
In sh2emu, from frame 502 the engine ran the cart image's sprite pixels as script code:

- On the SH2, `scriptCode` is the scene image's script part (R11A: 0xE304 entries).
- The "no script" index `SCRIPTDATA_COUNT - 1` (0x3FFFF) therefore lies in the image's graphics. Its value there is
  0x08090900, not 0.
- So `ProcessStartupObjects`, `ProcessObjects` and `DrawObjectList` ran garbage for every object type without that
  sub. The garbage reads at wild addresses (`0x2348B304`, `0x4A4CFC90`, `0x8E4BB728`...).
- Where those reads land depends on where the engine's tables lie. That is the likely reason the death came and went
  from build to build.

Fixed in R1R: `ScriptSubExists` in `engine/Script.hpp`.

**R1R.** On the console it ran the whole act, and the planes came on with a dot in every blank cell. In sh2emu,
`-shot` shows Palmtree Panic 1 drawn correctly from the SH2's records, with the same dots. Those come from the junk
record the 68000 took from COMM4's start-up value `1234`. R1S clears COMM4 first. sh2emu, with that line simulated
on R1R, draws the level with no dots.

**R1S (speed).** The console runs the game at 27-29 frames a second. `-profile` on R1S: each engine frame needs 1.55
vertical blanks on average and up to 2.56; about 45% of that is waiting for the cart. The slowest frames are the 3D
ramp's player script (a 128-turn script loop, 0.9 vertical blanks). R1T removed three pieces of pure waste (a variable
looked up by name every frame, the empty object slots, the scroll loop's per-line work): 1.31 on average, with the same
record contents CRC as R1S.

**R1U-R1W (speed).** Native scripts (R1U): 0.97. The hot code in SDRAM (R1V): 0.73, 99% of frames in one vertical
blank. R1W (`-pcprof` on the hot loops, the slowest-frames list): the empty-slot scan, a script sub that fell back to
the interpreter for 87,000 cycles in one frame, the foreground's line positions without a frame-buffer read per line,
the plane rows' name lookups and record writes: 0.59 on average, the slowest frame 0.83 - every frame in one.

**R3 (sprites, Tails).** The VDP model draws the sprites now, so `-shot` shows Sonic, Tails, the rings, the badniks
and the HUD as the SH2's records put them in VRAM. Two acts and two switches in one run (16,500 frames): `no problems
found`. Its profile found the slowest frame (1.13 vertical blanks) in the sprites' VRAM allocator: when 22 cell sets
had to make room, it evicted the least recently used one at a time and searched again after each. It now evicts one
run of neighbouring sets in one pass: the slowest frame 1.03, the same pictures (PC model: identical pixels), and 6%
fewer pattern uploads.

**R4 (every act, native scripts per scene).** Each scene image now carries its own scripts as native code, so sh2emu
treats the image's part 18 as code (it stopped with `EXEC OUTSIDE CODE` at first). Runs through the Level Select menu
(`-pad`, `-peek scdBlob`, `-syms`) and the build of every scene's blob found three things before any disc; after the fixes, every act as Sonic and as
Tails - 140 runs - `no problems found`:

- 1.4 million `R-UNDEF` a run in the acts with line deformation (Collision Chaos, Tidal Tempest ...): the foreground's
  line scroll entries, in the layout pool in the frame buffer, were never cleared - the original's are a static
  array of zeros. On the console: whatever the frame buffer held there - the foreground could take another line's
  deformation. Cleared when the act's layout loads.
- Stardust Speedway at 30-36 frames a second: its spotlights' scripts ran in the interpreter (the 3D buffer variables
  share one case in the interpreter, which reads the variable's number from the bytecode; the translator now puts in
  the number it knows), and so did every sub that calls a function by a variable number (every function of the
  scene is translated now). 59-60 after.
- The blobs of Collision Chaos 2 and 3 and Metallic Madness 3 did not link: a static flag inside the instruction
  `Set16x16TileInfo` is data, and a blob in the cart has none. The flag is a global of the engine now.
