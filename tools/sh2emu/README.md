# sh2emu: run a build's SH2 side on the PC before burning

`sh2emu` is an SH7604 (master SH2) interpreter with the 32X's SH2-side memory map and the SH2's 4 KB cache. It
runs a build exactly as it would go on the disc:

1. It boots the disc's own SH2 program, `SCDSH2.BIN` (Kobo's crt0 and the loader `cd32x/sh2_engboot.c`).
2. It loads the disc files through the same command 38 / 46 handshake the Sub-CPU answers on the console.
3. It starts the engine (`SONICCD.PAK`) and runs it frame by frame, with the vertical-blank interrupt at 60 Hz,
   Kobo's handlers and the R1E PC sampler.

The Sub-CPU and the 68000 are emulated only at the level of the COMM registers: the boot handshake and commands
38, 46, 48, 52 and 68.

It reports what kills or corrupts the console:

| Event | Meaning | On the console |
|---|---|---|
| `R-UNMAPPED`, `W-UNMAPPED`, `R-AREA`, `W-AREA`, `R-IO`, `W-IO`, `R-ROM`, `W-ROM` | reads or writes at addresses nothing answers on the 32X (reserved SH2 spaces, past the cart or SDRAM) | the SH2 can hang: no interrupt, line 10 freezes |
| `W-CARTCODE`, `W-LOADER`, `W-GAP` | writes into the engine's own code in the cart, into the loader or vector table, or below the stack | corruption |
| `STACK`, `EXCEPTION`, `ILLEGAL`, `ADDRESS ERROR`, `EXEC OUTSIDE CODE` | | crash |
| `STALE-READ`, `STALE-CODE`, `REG-CACHED` | the cache returned older data than memory holds | wrong data |
| `FB-NOFM`, `CART-RV` | frame buffer touched without FM, cart touched while the 68000 holds it | bus conflict |
| `R-UNDEF` (warning) | reads of memory nothing has written: the frame buffer and SDRAM are not cleared at power-on | random values |
| `*** LINE 10 FROZEN` | the sampler stopped, as on the console | the console's death signature |

The `STATE` lines are what line 10 shows on the console.

## Build and run (WSL)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/Sonic2011CD32X
gcc -O2 -o tools/sh2emu/sh2emu tools/sh2emu/sh2emu.c
# after make ... ENGINE=1, before burning:
tools/sh2emu/sh2emu cd32x/build_engine > sh2emu_log.txt; tail -25 sh2emu_log.txt
```

A run of 1500 frames (25 s of console time) takes about 15 seconds. The build folder needs `SCDSH2.BIN`, `sh2.map`,
`engine.map`, `engine_symbols.txt`, and `disc/` with `SONICCD.PAK`, `SCDDATA.PAK`, `L*.IMG` and `G*.MDG`. A normal
`make ENGINE=1` leaves all of these.

The run ends with a summary:

```
engine started    yes (frame 463)
stage set-up 5C16 yes (frame 467)
stage loaded      yes (STATE 5100, frame 591)
planes on (CMD 68) yes (frame 595)
...
VERDICT           no problems found on the SH2 side
```

`PROBLEMS FOUND - do not burn yet` lists each kind of problem with the first place it happened (function + offset,
from `engine_symbols.txt`). The log above the summary has the first few occurrences of each one, with the address
and the caller (PR).

## Options

```
-f N           run N frames (default 1500)
-v             every STATE change and every Sub-CPU chunk
-maxlog N      log lines per (event, PC) (default 4)
-zero          power-on RAM = zeros (default: pseudo-random); -seed N picks another pattern
-nocache       no SH2 cache model
-hook ADDR     print the registers whenever the SH2 reaches ADDR (up to -hookmax N, default 50)
-trace N COUNT print COUNT instructions from instruction N
-dumpsdram FRAME FILE   write the 256 KB SDRAM at FRAME (for checking engine tables)
-novbistop     keep running after line 10 freezes
```

## Limits

- It is not cycle exact. Frame timing is close: R1Q's first sample came out `8F34` / `9550`, where the console
  showed `8F33` / `9550`, in the same function at the same frame.
- Reads where nothing answers return 0 here. On the console they can return anything, or hang the bus. So the
  problem list is the useful part, not what the program does after the first problem.
- The 68000 and the VDP are not drawn. The Genesis planes show here only as records sent (COMM4 ids) and taken
  (COMM6).
- The Sub-CPU answers immediately (a few thousand cycles). On the console a 32 KB chunk takes about a second.

## Found with it

R1Q (the SH2 died after the stage set-up started, 8F33 / 9550 then nothing): from frame 502 the engine ran the cart
image's sprite pixels as script code. On the SH2, `scriptCode` is the scene image's script part (R11A: 0xE304
entries). The "no script" index `SCRIPTDATA_COUNT - 1` (0x3FFFF) lies in the image's graphics there (0x08090900, not
0). So `ProcessStartupObjects`, `ProcessObjects` and `DrawObjectList` ran garbage for every object type without that
sub, with wild reads (`0x2348B304`, `0x4A4CFC90`, `0x8E4BB728`...). Where those reads land depends on where the
engine's tables lie, which is the likely reason the death came and went from build to build. Fixed in R1R
(`ScriptSubExists`, `engine/Script.hpp`).
