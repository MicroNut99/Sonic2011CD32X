# Kobo Deluxe Sega CD32X – How It Boots and Runs

A guide for developers picking up the `kobo32x_hw` project (real hardware, Sega CD + 32X + MicroNut99's 4 MB RAM cart). It explains, in order, what happens from power-on to the game running, which processor does what, how the processors talk to each other, and the hardware rules that were learned the hard way.

Sources: `subcpu/main.c`, `subcpu/hw_md.s`, `subcpu/crt0.s`, `subcpu/crt.s`, `subcpu/cd.s`, `subcpu/scd.c`, `subcpu/Makefile`, `sh2/crt0.s`, `sh2/sh2_main.c`.

> **Marked "inferred":** a step that happens inside the toolchain's generic boot block (`/opt/toolchains/sega/bootblocks/US_BOOT.BIN`). That file is not part of this project; its behaviour is deduced from what our code expects.

---

## 1. The four processors and who runs what

| Processor | Program | Source | Role |
|---|---|---|---|
| **Sega CD Sub-CPU** (68000, 12.5 MHz) | `APP.BIN` | `subcpu/crt0.s`, `main.c`, `scd.c`, `cd.s` | **The coordinator.** Reads the CD (BIOS), drives the CD audio and PCM chip, writes the cart, and answers the SH2's requests. |
| **Genesis 68000** (7.6 MHz) | listener copied into its RAM at `0xFF1000` | `subcpu/hw_md.s` (`md_init_start`…`md_init_end`) | **A command slave** for the Sub-CPU. It does what only the Genesis side can do: Genesis VDP and text, controllers, the 32X registers and frame buffer, and the cart window. |
| **32X master SH2** (23 MHz) | `KOBO32X.BIN` (built as `D32XR.32x`) | `sh2/crt0.s`, `sh2/sh2_main.c` | **The game:** logic, drawing, menus. Asks the Sub-CPU for everything it cannot do itself. |
| **32X slave SH2** | same program | `sh2/crt0.s` (`sec_start`) | Started and parked in an idle loop. Not used by the game. |

**Key idea.** The SH2 cannot reach the CD or the Sega CD hardware. The Sub-CPU cannot reach the 32X or the Genesis VDP. The Genesis 68000 can reach both sides, but has no CD access. So every request travels **SH2 → (32X COMM registers) → Sub-CPU → (Sega CD comm registers) → Genesis 68000 → hardware**, and the replies come back the same way.

---

## 2. The disc

`subcpu/Makefile`, target `cd`, builds an ISO-9660 image with `genisoimage`:

- **System area / boot sector:** `US_BOOT.BIN` (`-generic-boot`), the toolchain's generic Sega CD boot block (security code, IP for the Genesis, SP for the Sub-CPU).
- **Files on the disc:**
  - `APP.BIN`: our Sub-CPU program (`CDROMPlayer.bin`, linked with `cd.ld`).
  - `KOBO32X.BIN`: the SH2 program. It is also embedded inside `APP.BIN` via `files.s`, which is the copy that is actually booted.
  - `KOBOGFX.BIN`: all graphics (201,540 bytes, one file).
  - `KOBOSFX.BIN`: the PCM sound pack (64 KB image + table).
  - `IMAGE.RAW`: the splash picture, 320×200 at 15 bits.
- **Audio tracks:** `make_mixed_cd2.py CDROMPlayer.iso Track02.wav Track03.wav Track04.wav` turns the ISO into a mixed-mode BIN/CUE.
  - Track 1: data
  - Track 2: title / menu / game music
  - Track 3: credits
  - Track 4: level transition

Build everything with `kobo32x_hw/build.sh`. It refuses to build unless both programs are in cart mode.

---

## 3. Boot, step by step

### Stage 0 – Sega CD BIOS
The BIOS checks the disc's system area and loads the boot block's IP (Genesis side) and SP (Sub-CPU side).

### Stage 1 – the generic boot block loads APP.BIN *(inferred)*
- The boot block's SP loads `APP.BIN` into Sub-CPU program RAM and calls its entry point `_start` (`crt0.s`: "CD startup at 0x8000").
- The boot block's IP keeps the Genesis 68000 waiting until there is code for it in Word RAM, then jumps there.

### Stage 2 – Sub-CPU start-up (`subcpu/crt0.s`)

**`_start`:**
1. Interrupts off.
2. Clear BSS.
3. Save the BIOS stack pointer.
4. Set our own stack.
5. Call `init_hardware`.
6. Run C initialisers.
7. Call `main()`.
8. If `main()` ever returns, restore the BIOS stack and return.

**`init_hardware`** (in `hw_md.s`, runs on the Sub-CPU) copies the **Genesis code** (`md_init_start` … `md_init_end`) to `0x080000`. That is Word RAM, which the Genesis sees at `0x200000`.

**`main()`**, first lines:
- `write_byte(0x8003, read_byte(0x8003) & ~1); while (read_byte(0x8003) & 2);`
- This hands Word RAM over to the Genesis side, so the Genesis can run the code from it.

### Stage 3 – the Genesis 68000 becomes a listener (`hw_md.s`)
1. The Genesis jumps into Word RAM at `md_init_start`.
2. `md_init_start` copies `md_start`…`md_init_end` to **`0xFF1000`** (Genesis work RAM) and jumps there. Address rule: runtime address = `0xFF1000 + (file offset − 0x174)`.
3. `md_init_hw` sets up the Genesis VDP:
   - display on, V-interrupt on (register 1 = `0x8174`);
   - Chilly's font in VRAM;
   - a three-line palette: line 0 white `0x0CCC`, line 1 green (now **black**, see §8), line 2 red;
   - installs `vblank_int` in the BIOS jump table at `0xFFFD08`.
4. **The listener loop** (`cmd_loop` / `md_check`) waits for commands from the Sub-CPU (§4.2).
5. **`vblank_int`, every frame:**
   - reads pad 1 and pad 2 → `0xA12018` / `0xA1201A`;
   - increments the tick counter → `0xA1201C`;
   - writes the counter to 32X COMM6 once the 32X is active (diagnostic heartbeat);
   - raises the Sub-CPU's level-2 interrupt (`0xA12000 |= 0x0100`).

### Stage 4 – waking up the 32X (`hw_md.s` `init_32x`)
The Sub-CPU copies the SH2 program (`sh2_app_start`, from `files.s`) into Word RAM, gives the bank to the Genesis (`switch_banks`), and sends `MD_CMD_INIT_32X (0x200000, length)`.

The Genesis 68000 then:

1. checks for **`"MARS"`** at `0xA130EC` (is a 32X there? else returns −2);
2. waits for the adapter to be ready (bit 7 of `0xA15101`; timeout → −5);
3. sets **ADEN** (`0xA15101 = 1`) and waits a moment;
4. clears COMM0–COMM6, sets ADEN + reset release (`0xA15101 = 3`);
5. gives the frame buffer to the Genesis (clears FM, bit 7 of `0xA15100`);
6. clears the 32X control registers;
7. clears **both frame buffers** (toggling FS in `0xA1518B`) and the 32X palette (`0xA15200`);
8. checks COMM0 for `"SDER"` (32X boot ROM self-test error → returns −3);
9. copies the SH2 program from Word RAM to the **frame buffer** at `0x840000`;
10. gives the frame buffer to the SH2s (sets FM);
11. writes **`"_CD_"`** to COMM0 – the signal for the SH2 boot ROMs to boot from the frame buffer;
12. waits for **`"M_OK"`** in COMM0 (master) and **`"S_OK"`** in COMM4 (slave);
13. clears COMM0/COMM4 and returns.

Chilly's **white** start-up lines ("Activated 32X", "Cleared 32X", "32X Waiting", "CD BOOT TABLE", …) are printed by `init_32x` (`message0`…`message6`).

### Stage 5 – the SH2 boot ROMs and `sh2/crt0.s`
**The 32X boot ROMs** (described in `sh2/crt0.s`):
- **Master:** waits for `"_CD_"`, reads the header at the start of the program in the frame buffer (destination `0x06000000`, length, entry points, VBRs), copies the program to **SDRAM `0x06000000`**, writes `"M_OK"` to COMM0, and jumps to `pri_start`.
- **Slave:** waits for `"_CD_"` and the master's `M_OK`, writes `"S_OK"` to COMM4, and jumps to `sec_start`.

**`pri_start`:**
1. sets up the stack;
2. clears `.bss`;
3. writes **COMM4 = 0** (releases the slave);
4. turns the cache on;
5. calls `main()`.

It also writes diagnostic checkpoints to COMM10 (`0x0001`…`0x0003`).

### Stage 6 – the start handshake (COMM10)
**SH2 `main()`:**
- writes checkpoints `0x0004`…`0x0007` to COMM10;
- enables interrupts;
- writes **`0x0AAA`** to COMM10 ("SH2 ready");
- waits for **`0x0BBB`**.

**Sub-CPU `main()`, after INIT_32X:**
- writes `0x1234` to COMM4 (legacy, nobody waits for it any more – see §7);
- polls COMM10 (through the Genesis) for `0x0AAA` (3 s timeout);
- checks the cart (`safe_cart_present`);
- writes **`0x0BBB`**.

The SH2 then takes the frame buffer (`take_fb`) and starts loading. The `0x0AAA`/`0x0BBB` exchange works in either order: each value stays in COMM10 until the other side reads it. (The old design also waited for COMM4 = `0x1234`; that raced with `crt0.s` clearing COMM4 and was removed in K28.)

### Stage 7 – the Sub-CPU's main loop and the game's start-up
The Sub-CPU then:
- runs `InitCD`;
- prints the CD track info;
- enters its **main loop**, serving SH2 commands (§4.1).

The SH2 start-up posts a status code in COMM12 after each step. The Sub-CPU shows it as `STATE:xxxx` when the green text is on.

| Code | Step |
|---|---|
| `4B01` | Load start: `CMD 38` opens `KOBOGFX.BIN`; `CMD 46` copies it in 32 KB chunks into the **cart** at 2 MB (cart mode, Chilly's RV method, every chunk verified). |
| `4B02` | Pack in the cart. |
| `4B03` | Graphics copied from the cart into SDRAM (the SH2 reads the cart at `0x22000000`). |
| `4B04` | Sound pack loaded into the PCM chip (`CMD 53`). |
| `4B05` | High scores and options read from backup RAM (`CMD 56`/`57`). |
| `4B51`–`4B54` | Palette loaded, level 1 built, 256-colour mode and text set (`CMD 52`), buffer swaps done. |
| `4B6n` / `4B7n` | Tile set *n* copied from the cart / written into both frame buffers (spare memory). |
| `4B06`, `4B07` | Tile sets done; panel drawn. |
| `4B08` | **Music started** (`CMD 45`). Rule: **no CD data reads after this point.** |
| `4B10` | Main loop running: menu, game. |

If `KOBO_SPLASH 1`, the splash (`CMD 51`, `IMAGE.RAW` straight into the frame buffer) is shown before the loading, and the Genesis text layer is switched off at that point.

---

## 4. How the processors talk at runtime

### 4.1 SH2 ↔ Sub-CPU: the 32X COMM registers

| Register | SH2 address | Genesis address | Use |
|---|---|---|---|
| COMM0 | `0x20004020` | `0xA15120` | **Command** from the SH2 (low byte), plus a sound-effect id in the high byte for `CMD 48`. Cleared to 0 by the Sub-CPU when done. |
| COMM2 | `0x20004022` | `0xA15122` | Command parameter (file number, track, chunk, word index…). |
| COMM4 | `0x20004024` | `0xA15124` | Start-up only (`S_OK`, legacy `0x1234`). |
| COMM6 | `0x20004026` | `0xA15126` | Genesis vblank counter (diagnostic heartbeat `V`). |
| COMM8/10 | `0x20004028`/`2A` | `0xA15128`/`2A` | Replies: pad (COMM8) + Genesis tick counter (COMM10) for `CMD 48`; file length; start-up handshake in COMM10. |
| COMM12 | `0x2000402C` | `0xA1512C` | SH2 status code (`STATE`); 16-bit command replies (cart chunk result, sound/backup-RAM results). |
| COMM14 | `0x2000402E` | `0xA1512E` | Sub-CPU heartbeat (`P`): drive-status nibble + pass counter. |

**SH2 commands** (`sh2_main.c` sends, `main.c` handles):

| Cmd | Name | Action |
|---|---|---|
| 38 | open file | COMM2 = file number. Reply: length in COMM8. |
| 45 | play CD track | COMM2 = track; BIOS play-repeat. |
| 46 | chunk → cart | Cart mode; verified write. |
| 48 | pad | Fast path at the top of the Sub loop. Reply: pad + ticks in COMM8/10; the high byte of COMM0 starts a sound effect. |
| 50 | file chunk → frame buffer | No-cart mode: 16 KB, off-screen. |
| 51 | file chunk → frame buffer | Splash: 32 KB, at an offset. |
| 52 | Genesis display on/off | Text layer. |
| 53 | load sound pack | → PCM chip. |
| 54 | stop music | BIOS `MSCSTOP`. |
| 56 / 57 / 58 / 59 | backup RAM | Load / get word / put word / save. |

**How a command works:**
1. The SH2 writes COMM2 (and COMM8) if needed, then the command into COMM0.
2. It waits (with ~20 µs pauses between reads) until COMM0 is 0.
3. It reads the reply.

### 4.2 Sub-CPU ↔ Genesis 68000: the Sega CD comm registers (`hw_md.s`)
**Sub-CPU side (`do_md_cmdN` → `send_md_cmd`):**
1. writes up to four long arguments to `0xFF8020…0xFF802F`;
2. waits until the Genesis flag (`0xFF800E`) is 0;
3. writes the **command number** to its flag (`0xFF800F`);
4. waits for the Genesis to **echo** it in `0xFF800E`;
5. clears its flag;
6. waits for the Genesis flag to return to 0;
7. reads the result (`0xFF8010`).

**Genesis side (`md_check` → dispatch → `cmd_done`):**
1. reads the Sub flag (`0xA1200F`) **twice** and only acts if both reads match (HW10, see §6);
2. echoes the command to `0xA1200E`;
3. pushes the four arguments (`0xA12020…`);
4. calls `cmd_table[cmd]` (`jsr`, returns at `0xFF10AE`);
5. writes the result (`0xA12010`);
6. waits (reading twice) for the Sub flag to be 0;
7. clears its own flag.

Commands used at runtime: `READ_32X_16/32` (19/21), `WRITE_32X_16/32` (20/22), `SET_VDP_REG` (34), `PUT_STR` (9), `CHILLY_RV_MEMCPY` (33), `INIT_32X` (18), `INSTALL_EXC` (31). Command 2 (`set_sr`) is disabled.

---

## 5. Memory maps

**SH2 SDRAM (256 KB at `0x06000000`)**

| Range | Contents |
|---|---|
| `0x06000000` | Program (must end below `0x0600C000`; checked at start-up, status `4BE6`). |
| `0x0600C000` | **WORK2:** fighter, boomerangs, bigship, rock2/3, enemy table (128), gun list (512). |
| `0x0601EC00` | RLE explosions (explo5, bolt, bullet). |
| `0x06020000` | **WORK:** map, maze sites, tiles, stars (32), pack header, player, bolt, logo (in the unused damage table), enemy4, beam, explosion, font, enemy1/3, rock1, ring, enemy6, bomb, detonation, RLE explo3/4. |
| `0x0603CE00` | RLE rock and ring explosions. |
| up to `0x0603F400` | Master stack. |

**32X frame buffer (each of the two buffers, 128 KB)**

| Range | Contents |
|---|---|
| `0x00000`–`0x001FF` | Line table. |
| `0x00200`–`0x11A00` | The 320×224 picture. |
| `0x12000`–`0x16000` | No-cart loading window. |
| `0x16000`–`0x1D800` | Cache of the 5 tile sets. |
| `0x1D800`–`0x1FFFF` | Free. |

Write the frame buffer with **16-bit writes** (§6).

**Cart (4 MB):** `KOBOGFX.BIN` at cart offset 2 MB (slot 16 × 128 KB). The Genesis reaches it through the 32X bank window `0x900000` (bank register `0xA15104`); the SH2 reads it at `0x22000000 + offset`.

**Word RAM:** 1M mode. The Sub-CPU's bank is at `0x0C0000` and is used as a CD read buffer and for text strings. `switch_banks()` swaps the banks between the CPUs.

---

## 6. Hardware rules learned on the console (Fusion shows none of these)

1. **Torn reads of the command flag.** The Genesis read `0xA1200F` while the Sub-CPU was changing it and got 2 instead of 19. That ran `set_sr` with a garbage argument: `SR = A71E`, trace bit on, interrupts off. Results: a red `68K EXC V09` (frame `A71E 00FF 10AE …`), or a silent deadlock about 19 s after boot, because the echo never matched. The same bug caused the old V70 crash (`00FF10AE`). **Fix:** read twice and compare, in `md_check` and in `cmd_done`; disable unused dangerous commands.
2. **COMM registers:** do not hammer a register while the other CPU writes it. Use one writer per register, poll with pauses, and avoid 32-bit writes that also hit another CPU's half (COMM12/14).
3. **No CD data reads after the music starts.** The drive cannot read data and play audio at the same time: the track keeps counting but is silent.
4. **Frame buffer:** write with 16-bit writes. Byte writes of `0x00` did not land in Fusion.
5. **Colour `0x0000` is see-through** on the 32X (the Genesis layer shows). Draw black as `0x0400`.
6. **The cart copy path:** keep the pack small; unused banks are written empty by `kobo_gfx.py`.
7. **Diagnostic text is expensive:** each line swaps Word RAM and goes through the Genesis. Reprinting every frame dropped the game to 1–2 fps.
8. **The SH2 build has no C library:** no variable shifts, no signed shifts by more than 1, no divisions by variables. The project's own `memset`/`memcpy`/`memmove` live in `sh2_main.c`.
9. **The program must end below `0x0600C000`**, and big arrays must not go in `.bss`. `crt0.s` clears `.bss` before releasing the slave; a long clear once raced the start handshake.

---

## 7. Leftovers in the code (harmless, kept as history)

- **`main.c`, in the Sub-CPU's start-up sequence:**
  - `InitCD()`, `memcpy(temp, Sub_Start, …)`, `send_md_cmd0('I')`;
  - "INITIALIZING SUB-CPU Sfx…", "68K LISTENER ACTIVE", "BOOTING SUB-CPU…";
  - "(TEXT BLINKS TO SHOW SH2 IS ALIVE)".

  These come from an older design where the program ran on the Genesis and booted the Sub-CPU itself. `scd.c` keeps that version under `#if 0`.
- **The `0x1234` write to COMM4:** nothing waits for it since K28.
- **"TOO TALL" after the file size:** a label from an old picture viewer. Not an error.
- **The `'M'` command in the listener:** jumps into Word RAM (`0x200000`); unused.
- **The `d7` branch in `cmd_loop`:** lets the Genesis serve SH2 commands directly; `d7` is 0, so it is unused.
- **`crt.s`:** a complete Sub-CPU system program (`SPMain` with its own command loop), embedded via `cd.s`. Its level-2 interrupt (`SPInt2`) is only `rts`.

---

## 8. Switches (`sh2/sh2_main.c` / `subcpu/main.c`)

| Switch | Release value | Meaning |
|---|---|---|
| `KOBO_NOCART` (both files) | 0 | Cart mode (hardware). 1 = no-cart (Fusion). |
| `KOBO_SPLASH` | 1 | Splash while loading. |
| `KOBO_HW_PCM` / `KOBO_HW_BRAM` | 1 / 1 | Sound effects / backup RAM. |
| `KOBO_HW_DIAG` | 0 | Bottom-left readout: P (Sub heartbeat + drive status), V (Genesis vblanks), C (command), T (pad timeouts), F (fps). |
| `KOBO_DIAG_TEXT` (`main.c`) | 0 | Green status text. The green colour is also set to **black** in `hw_md.s` `md_init_hw`; restore `0x00A0` to see it. |
| `KOBO_BOOT_TEXT` (`main.c`) | 1 | Keep the Genesis display on during boot (white lines). |
| `KOBO_HW_QUIET` (both) | 0 | Diagnostic: no CPU traffic after the menu appears. |
| `KOBO_FIXED_STEP` / `KOBO_STEP_MS` | 0 / 30 | Fixed-time game logic (off = one step per frame). |
| `KOBO_DEBUG_KEYS` | 0 | Debug extras. |
