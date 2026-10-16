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
it saves the Genesis picture as a `.bmp` file, so you can see what the console should show before you burn.

Its timing matches the console (measured on R1S): the game speed it reports is the speed you will see. With
`-profile` it shows where the SH2's time goes (section 4).

## 1. Build it (once)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X/tools/sh2emu/sh2emu.c
```

This uses the PC's own gcc, not the Sega toolchain. Build it again only when `sh2emu.c` changes.

## 2. Run it on every build, before burning

Build the disc as usual (`make ... ENGINE=1`). That leaves everything sh2emu needs in `cd32x/build_engine`. Then:

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
| `planes on (CMD 68)` | the SH2 asked for the Genesis planes and the Sub-CPU accepted |
| `game speed` | engine frames per second of play, after the stage loaded. The game is made for 60; R1S: 26-29 |
| `plane records` | records the SH2 sent, the 68000 took, and the VDP got. `INCLUDING A JUNK RECORD` = COMM4 held a value when the planes came on, and the 68000 took it as a record: a dot in every blank cell (R1G, R1R). Listed as the problem `JUNK-RECORD` |
| `record contents` | a CRC of the first 600 records (option `-reccrc N`). It depends only on the pictures, not on the speed: a speed change that keeps this CRC keeps every picture the same |
| `frame-buffer FIFO` | (only when it happens) words the SH2 wrote to the frame buffer that were still in the 32X's 4-word write FIFO when it gave the frame buffer to the 68000. The 32X does not guarantee them |
| `line 10 now` | what line 10 would show at the end |
| `PROBLEMS` | **do not burn.** Each kind of problem is listed with how many times it happened and where it happened first (the function and the offset into it) |
| `warnings` | reads of memory nothing has written; worth a look, not a reason to stop |
| `information` | normal register traffic, listed for reference |
| `VERDICT` | `no problems found` = safe to burn as far as the SH2 and the plane records go |

Emulated time is counted in frames: 60 frames are 1 second on the console.

The earlier part of the log lists, as they happen:

- the **STATE lines**, which are what line 10 shows on the console;
- the Sub-CPU's file opens;
- the **event lines**.

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
-reccrc N      the record contents CRC over the first N records (default 600)
-profile A B   where the SH2's time goes between frames A and B (section 4)
-callers NAME  with -profile: who calls the function NAME, how often a frame
-pcprof NAME   with -profile: NAME's every instruction - count, cycles, stalls per address (up to 8 functions)
-frames A B    with -profile: every engine frame that ended in console frames A..B, with the functions that took it
-cartwait N    cart waits for a single access (default 15); -cartrd N / -cartfill N set the read / line costs directly
-fbread N      frame buffer read cost (default 8); -k68word N the 68000's cycles per record word (default 75)
-nofifo        no frame-buffer write FIFO model; -fbword N = SH2 cycles per FIFO word (default 5)
-novbistop     keep running after line 10 freezes
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
  console copied on R1H (`REC 0004 0004 0006 ...`). The VDP model draws the two planes, their scroll and the colours.
  It has no sprites, no window, and no 32X layer. The 68000's copy takes 75 SH2 cycles a word, and it does not take a
  record while its vertical blank handler runs (`-k68word`).
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
