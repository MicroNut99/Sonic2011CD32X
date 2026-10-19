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
  act 1. Then the act starts as always.
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

**Unchanged since R4F.** `sub_main.c` as R4F (R4E's page switch `0xF8nn`, R4F's slot `0xF0nn` - the only commands the
preload uses besides 38 and 46), `hw_md.s` unchanged, the SH2 loader (`SCDSH2.BIN`, `SH2-VR4D`) unchanged. The power-on
still reads the engine, the data pack and the title from the disc, as today.

## Checked before burning

RESULTS

## Build

BUILD

## When the new cart exists

WHEN
