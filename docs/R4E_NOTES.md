# R4E – the cart test for the 8 / 16 MB cart

**Goal of the R4E disc:** be ready for MicroNut99's new cart. The Options Menu gets a **`CART TEST`** row that tries
the cart's page mapper and reports what it found. Everything else plays as R4D. **There is no reason to burn R4E for
the 4 MB cart** - burn it when the new cart exists, and run the test before the game uses the pages (the 16 MB game
mode is the next step: `docs/CART16_HANDOFF.md`).

## The cart (your spec, 19 Oct)

Two windows of 2 MB - window 0 = cart `0x000000-0x1FFFFF`, window 1 = `0x200000-0x3FFFFF` - each showing one of 8
pages of 2 MB, selected with an upper-byte write (page in D8-D10) to `0xA130F0` (window 0) or `0xA130F2` (window 1),
with RV = 1 and interrupts off; window 0 = page 0 and window 1 = page 1 at power-on and reset (today's 4 MB); an 8 MB
build shows pages 4-7 as 0-3; the SH2 caches purged after a switch.

## The boot stack

- **The Sub-CPU (`cd32x/sub_main.c`): one addition.** Command 46 with COMM2 = `0xF800 | window << 8 | page` writes the
  page register. No new command number: a chunk run's COMM2 is at most `0x03FF`, so `0xF8xx` was never sent before.
  The game itself never sends it - only the `CART TEST` row.
- **The 68000 (`cd32x/kobo/hw_md.s`): unchanged.** The Sub-CPU uses the listener's existing command 26
  (`SAFE_CART_XFER`): interrupts off, RV = 1, a one-byte copy to `0xA130F0` / `0xA130F2`, RV = 0, interrupts back - your
  recipe exactly.
- **The SH2 loader (`cd32x/sh2_engboot.c`, `SCDSH2.BIN`): unchanged** (the same file as R4D's, `SH2-VR4D`).

## CART TEST

The last row of the Options Menu (A + B + C + Start; from `START GAME`, Up twice). A or Start runs it; it takes a few
seconds (CD reads), the screen stands still meanwhile, then the result is in the two lines under `SEGA CD32X`:

1. **Read only, first.** Window 1 is switched to page 0: if it then shows the same bytes as window 0, the switch works.
   Page 4: if it shows page 0's bytes, the cart is 8 MB. Each page to be tested that shows page 0's bytes is not
   written (it would be the engine's code). **Window 0 is never switched, pages 0 and 1 (the game's) never written.**
2. **Write.** Each of pages 2-7 (8 MB: 2-3) gets its own 32 KB pattern (chunk p of the test file `KOBOGFX.BIN`) at
   window 1 + p × 32 KB; the Sub-CPU reads each back through the 68000 and compares.
3. **Read back on the SH2.** Each page switched in again, the cache purged, the pattern read through the cache; and
   no page may show another page's pattern (nor page 1).
4. Window 1 back to page 1.

| The two lines | Meaning |
|---|---|
| `CART 16 MB: PAGES 2-7 OK` / `PAGE SWITCH OK, GAME PAGES UNTOUCHED` | the mapper works: the next disc can use the pages |
| `CART 8 MB: PAGES 2-3 OK` / ... | an 8 MB build (pages 4-7 = 0-3), working |
| `CART TEST: NO PAGE MAPPER` / `(A 4 MB CART) - NOTHING WRITTEN` | window 1 did not change: no mapper (today's cart), or the register writes do not reach it |
| `CART 16 MB: OK 2 3 4 6 7` / `BAD 5` | page 5 did not keep what was written (the 68000's read-back or the SH2's) |
| `... SAME RAM 4 6` | pages 4 and 6 show each other's patterns: a page bit not decoded |
| `... AS PAGE 0: 2 3` | those pages show page 0: not written (a page bit not decoded) |
| `CART TEST: KOBOGFX.BIN NOT ON THE DISC` | the test file is missing (the build's copy step) |

On your 4 MB cart the test writes the two page registers (`0xA130F0`, `0xA130F2`) and nothing else: a cart without
the mapper ignores them, and the result is `NO PAGE MAPPER`. (If your 4 MB cart decodes that address area for
something else, don't run it there.) After a test with problems, press reset before playing.

@@CHECKS@@
