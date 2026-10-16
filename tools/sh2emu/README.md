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

To see the Genesis picture, add `-shot FRAME FILE.bmp` (up to 8 of them) and open the `.bmp` files in Windows:

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
| `plane records` | records the SH2 sent, the 68000 took, and the VDP got. `INCLUDING A JUNK RECORD` = COMM4 held a value when the planes came on, and the 68000 took it as a record: a dot in every blank cell (R1G, R1R). Listed as the problem `JUNK-RECORD` |
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

## 4. Options

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
-shot FRAME FILE.bmp    save the Genesis picture (planes model) at that frame; up to 8
-records N     print the first N plane records the 68000 model applies (default 3)
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

## 5. Limits

- **Timing.** It is not cycle exact, but frame timing is close. R1Q's first sample came out as `8F34` / `9550`. The
  console showed `8F33` / `9550`: the same function, in the same frame.
- **After the first problem.** Reads where nothing answers return 0 here. On the console they can return anything,
  or hang the bus. So the first problem is what counts, not what the program does after it.
- **The 68000 and the VDP are models.** The model follows `sgb_poll` and `sgb_vblank` exactly. When the 68000 takes
  an id while the SH2 still holds the frame buffer, the console reads undefined data; the model then uses what the
  console copied on R1H (`REC 0004 0004 0006 ...`). The VDP model draws the two planes, their scroll and the colours.
  It has no sprites, no window, and no 32X layer. The 68000's timing is not modelled: it takes a record the moment the
  SH2 hands it over.
- **Loading speed.** The Sub-CPU answers at once. On the console a 32 KB chunk takes about a second, so the loading
  part of the timeline is much shorter here.

## 6. What it found

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
