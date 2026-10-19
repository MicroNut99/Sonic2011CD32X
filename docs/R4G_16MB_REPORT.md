# Sonic CD CD32X, R4G: how the 16 MB cart is used, and exactly what changed in the boot stack

Written 19 Oct 2026 for another AI working with MicroNut99. It replaces a shorter summary given the same day, which
was misleading in three places (section 0). Everything here is from the code in this repository and the cart's repo,
MicroNut99/16MB-PCB-CD32X. **Proof level: sh2emu only.** The 16 MB cart has not been built, and no disc with these
changes has been burned yet (R4D is the last disc proven on the console).

## 0. Corrections to the earlier summary

1. **"No boot-stack edit" was wrong.** The earlier summary was titled "without new boot-stack commands", and before
   that the owner was told R4G needed no boot-stack edit. Both compared R4G with R4F, a disc that was never burned.
   Against R4D (the last working disc) **the Sub-CPU's program, `cd32x/sub_main.c`, is changed: 23 lines, two new
   cases inside the existing command 46.** There are no new command numbers, but it is a boot-stack edit. The owner
   accepted it on 19 Oct as the minimum the 16 MB cart needs (section 2 says why).
2. **"Detection only reads the cart" was loose.** It writes no cart RAM, but it does write the window 1 page
   register: twice on a 4 MB cart, three times on an 8 / 16 MB cart. R4D never wrote that register.
3. **"R4G takes three ideas from OpenJazz" overstated it.** No OpenJazz code is used for the cart. OpenJazz CD32X REV
   never switches pages (it is a 4 MB cart game). Its notes (`JazzBuildNotes_10062026_05.pdf`,
   `OPENJAZZ_CD32X_HANDOFF.md`) describe the same approach R4G takes:
   - load with the existing commands 38 / 46 during a pause where the CD audio is off anyway;
   - load what comes next within a group (Jazz: both levels of a planet in one pack);
   - after power-on, check the cart against an identity mark and a checksum instead of reading it again.

   In the OpenJazz clone read here (15 Oct 2026) only the per-planet packs are built. Preloading into a second slot
   and the warm-boot check are listed as next steps. A later Jazz build was not checked.

## 1. The chain of command, and why the boot stack had to change

- **The SH2 (the engine, `cd32x/engine/plat_mars.cpp`)** writes a command number into the 32X COMM register COMM0,
  with its arguments in COMM2 / COMM8, and waits until COMM0 is 0 again. The SH2 cannot write the cart. It also cannot
  reach the 68000's I/O space, where the page registers are.
- **The Sub-CPU (`cd32x/sub_main.c`)** reads the CD. It reads the COMM registers through the Genesis 68000 and tells
  the 68000 what to copy where.
- **The Genesis 68000 (`cd32x/kobo/hw_md.s`)** does the cart writes. Its command 26, `SAFE_CART_XFER`, turns
  interrupts off, sets RV = 1, and copies bytes from Word RAM to an address it is given.

In R4D every cart write the Sub-CPU asks for goes to the cart base plus a 128 KB slot plus an offset, all inside the
first 4 MB. **None of R4D's commands lets the SH2 choose the address the 68000 writes to.** So with R4D's boot stack
unchanged, the page registers at `0xA130F0` / `0xA130F2` can never be written, and the game can never see more than
the first 4 MB. A Sub-CPU change is the only way to reach the other pages.

## 2. The boot-stack change, exactly (`git diff 41ab7cc HEAD -- cd32x/sub_main.c`)

Both cases sit inside the existing command 46 (`CMD_CHUNK_TO_CART`), checked before its chunk runs:

| COMM2 | Added in | What the Sub-CPU does | Reply |
|---|---|---|---|
| `0xF800 \| window << 8 \| page` | R4E | puts `page & 7` into Word RAM, then the 68000's existing command 26 copies that byte to `0xA130F2` (window 1) or `0xA130F0` (window 0): an even-byte write, so the page lands in D8-D10 | COMM12 = 0, COMM0 = 0 |
| `0xF000 \| slot` (0-31) | R4F | `cur_img = slot`: the file command 38 opened goes to 128 KB slot `slot` (cart `slot × 0x20000`) instead of its own, until the next command 38 | COMM12 = 0, COMM0 = 0 |

- A chunk run's COMM2 is `first | more << 8` and never reaches `0xF0xx` or `0xF8xx`, so R4D's commands behave as
  before.
- Unchanged since R4D: `hw_md.s` (no new 68000 command), the SH2 boot loader (`cd32x/sh2_engboot.c` →
  `SCDSH2.BIN`, label `SH2-VR4D`, md5 `0bd33013668e2c6fa4c981e5146dd290` in R4D, R4F and R4G alike), and the
  power-on: the engine, `SCDDATA.PAK` and the title are read from the disc every time.
- R4G adds nothing to the boot stack beyond R4E and R4F. **Neither the R4E nor the R4F disc was burned, so the R4G
  disc is the first to run the new `sub_main.c` on the console.**

Why the second case is needed: an act page must hold the engine's copy at its own `0x000000` and an act scene's image
at its own `0x080000`, written through window 1. R4D's file-to-slot table puts the engine at slot 0 and images at
slot 4, which are window 0 addresses. Window 0 cannot be pointed at a page that does not hold the engine yet, because
the SH2 is running from it.

## 3. The cart (MicroNut99/16MB-PCB-CD32X README)

- Eight pages of 2 MB, seen through two 2 MB windows: window 0 is cart `0x000000-0x1FFFFF`, window 1 is
  `0x200000-0x3FFFFF`.
- Page registers: `0xA130F0` (window 0) and `0xA130F2` (window 1), in the 68000's `/TIME` area. Even byte only, page
  in D8-D10, write-only.
- After power-on or reset the windows show pages 0 and 1, which looks exactly like the 4 MB cart.
- The 8 MB build aliases pages 4-7 onto 0-3.
- Battery-backed: the pages keep their contents through power-off.
- The SH2 can read the cart but not write it. After a page switch it must purge its cache.

## 4. How the 16 MB are laid out (engine side)

| Pages | Seen in | Holds |
|---|---|---|
| 0, 1 | windows 0, 1 | the 4 MB game exactly as the boot loader builds it at every power-on: the engine at `0x000000`, the scene image at `0x080000` (menus up to 2,306,768 bytes, spilling into window 1), `SCDDATA.PAK` with the loading picture at `0x300000`, one act file slot at `0x380000` |
| 2-6 ("act pages") | window 0 | each: its own copy of the engine at `0x000000`, byte for byte the running one (400,864 bytes), and one act scene's image at `0x080000` (up to 1,127,120 bytes), used where it lies |
| 7 | window 1 | the act files (up to 454,260 bytes) of act pages 2-5, in 512 KB slots at window 1 offsets `0x200000`, `0x280000`, `0x300000`, `0x380000`. Page 6's act file uses page 1's `0x380000` slot, which the dev menu's font, the credits and the title demos also use |

- Why in place: the native scripts are linked to `0x02080000 + offset`, so an act's image must be at window 0's
  `0x080000`.
- Why each page needs the engine: window 0 is switched while the SH2 runs from it. The SH2 waits in SDRAM
  (`sd_cmd`) while the 68000 writes the register, and comes back to the same bytes.
- Act files are read by R4D's unchanged command 68 (op 1), which takes the file's offset from the SH2 (COMM8) and
  reads it through the 68000's `0x900000` bank window. Each 512 KB slot lies inside one 1 MB bank, as that command
  requires.

## 5. Detection (`big_detect`, when the title starts)

1. Window 1 is switched to page 0 (`0xF8nn`). Window 1 is compared with window 0 (uncached), 4 KB at the engine
   (`0x10000`) and 4 KB at the scene image's header (`0x080000`).
   - No match: no mapper, so 4 MB. Window 1 goes back to page 1 and nothing else happens (two register writes in
     all).
   - Match: window 1 is switched to page 4 and compared again. A match there means 8 MB (page 4 is page 0
     again); otherwise 16 MB. Then back to page 1 (three writes in all).
2. No cart RAM is written. The result is kept until power-off. On the Options Menu, the `BIG CART` row shows it and
   can turn it off.

## 6. Filling and using an act page

**Filling page P with scene X** (all with the existing commands 38 / 46, plus the two new cases):
1. Window 1 → P (`0xF8nn`).
2. The engine copy, if P's isn't already identical: open `SONICCD.PAK` (38), slot 16 (`0xF010`, which is window 1's
   `0x200000`, P's `0x000000`), chunk runs (46). Then it is compared byte for byte, uncached, with the running engine.
   **Window 0 is never switched to a page whose engine was not compared.**
3. The image: open X (38), slot 20 (`0xF014`, P's `0x080000`), chunk 0 (46). Then only the chunks whose CRCs differ
   from what P held, if P's old image was whole; otherwise all of them.
4. The act file: window 1 → its slot's page, open it (38), slot override to the slot, chunk runs.
5. The SH2 purges its cache after every page switch and after every load.

**Using page P:** window 1 → its act file's page, window 0 → P (two register writes and a cache purge), then the
engine's usual stage set-up. No disc.

## 7. The preload (`big_prepare` / `big_preload`)

- **When:** at an act's start, meaning the first scene of an act, from a save, the menu or the end of the act before.
  Not in title demos (the engine variable `Options.AttractMode` is checked).
- **What:** the act's other time periods and the next act's start go into the other act pages, behind the loading
  picture and its bar. The CD audio is off during it (a CD data read stops it anyway).
  - Order: present, past, the next act's start, bad future, good future. The last goes into page 6, the one whose act
    file shares page 1's slot.
  - The next act's start: after act 1, act 2's present; after act 2, act 3's bad future; after act 3, the next zone's
    act 1.
- **Then:** time warps, restarts and the next act's first scene are page switches. In sh2emu, four real time warps
  (present → past → present → bad future → present) opened no file at all.
- **Cost (computed, not measured):** up to about 6 MB per act start (about 1.1 MB image plus 0.43 MB act file per
  page), about 40 s at 150 KB/s plus seeks. The first time ever add each page's engine copy, 5 × 400 KB, about 13 s
  more. Time Attack pays it for every new act too.

## 8. After a power-on (`big_identify`, the battery)

- `tools/build_preload.py` appends a table to `SCDDATA.PAK`, which the boot loader reads into the cart at every
  power-on. Layout:

  ```
  [data pack][loading picture][entries][16 B 'PRE1', offset, count, 0][16 B 'SPL1' footer]
  ```

  Each entry is 16 bytes: u16 file number, u16 kind (1 image, 2 act file), u32 length, u32 the boot loader's checksum
  (`scd_cart_sum`) over the length rounded up to 2048, and u32 an FNV-1a fingerprint of the head. The head is an
  image's header to the end of its chunk-CRC list, or an act file's first 4 KB. 280 entries; the pack is 131,712
  bytes.
- At the first act scene after power-on, each act page's image and each act file slot is fingerprinted, looked up,
  and checked whole with the checksum.
- Whatever fails is treated as empty. A page half-loaded when the power went off is loaded again in full, and its
  header's chunk list is not trusted. Checking takes a few seconds and needs no disc.

## 9. 8 MB, 4 MB and the Options Menu

- **8 MB:** one act page (2), its act file in page 3. It keeps the act's first scene, through a power-off too. The
  act's other periods load in page 0 as on the 4 MB cart, so a warp back to the first scene is a page switch.
- **4 MB:** the game is R4D's, plus the two page-register writes at the title (section 5). Whether the owner's 4 MB
  cart (v01) uses `/TIME` for anything is an open question: the 16 MB repo has v01's edge connector, where the `/TIME`
  pad B31 is a named net, but not its board.
- **The Options Menu** (A + B + C + Start): the last row is `BIG CART   16 MB / 8 MB / OFF`.
  - A on it turns the mode off, or runs the R4E cart test and turns it on if the test passes.
  - The test writes 32 KB patterns only into pages it proved are not page 0 (read back by the Sub-CPU and by the
    SH2), and never writes pages 0 / 1.
  - It refuses to run while page 0 holds an act scene, because an act page with the same scene would look exactly
    like page 0.

## 10. What was checked, and what was not

Checked, sh2emu only, all `VERDICT no problems found`, on a build made from scratch with the owner's commands:
- the 4 MB power-on: the same pictures as R4E's and R4F's runs (record CRC `2F19502C`) and the same loads, except
  one more 32 KB chunk of `SCDDATA.PAK` at power-on (the table: 131,712 bytes, 5 chunks instead of 4), plus the 2
  register writes;
- the 16 MB first power-on with the preload, a warp, and act 2's start with its preload;
- a second power-on with the cart kept (`-cartkeep`), where the act's scenes came from the cart;
- a power cut in the middle of a preload;
- 8 MB;
- the cart test after a preload, and on 4 MB ("NO PAGE MAPPER");
- the test refused with an act in page 0;
- title demos;
- a title demo, then a game (the preload still runs);
- Tails;
- real time warps on 16 and 8 MB.

sh2emu models the mapper (`-cart 4/8/16`), the battery (`-cartkeep FILE`), stale cache reads after a switch
(`STALE-READ`), and window 0 switched to a page without a compared engine (`CART-W0`).

Not checked:
- anything on the console, including the new `sub_main.c` cases;
- whether the 4 MB v01 cart ignores the `/TIME` writes;
- GreenPAK timing;
- the real CD speed (sh2emu's load times are not the console's).

A review by another model (Fable) read the whole repository. It found the cart-test case (now refused), missing size
guards (added), and the 4 MB `/TIME` question (open). It found the bookkeeping, the cache handling and the table's
format correct.

## 11. Files

- `cd32x/engine/plat_mars.cpp`: the "THE PRELOAD" block (`big_detect`, `big_prepare`, `big_preload`, `big_identify`,
  `big_engine`, `big_targets`), `scene_load_at`, `plat_cart_test`.
- `cd32x/sub_main.c`: command 46, the `0xF8nn` / `0xF0nn` cases.
- `tools/build_preload.py`, `tools/sh2emu/sh2emu.c`.
- `docs/R4G_NOTES.md` (the build, the burn, what to expect), `docs/CART16_HANDOFF.md` (the cart, the history,
  what is still to do).
