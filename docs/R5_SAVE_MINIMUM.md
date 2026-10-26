# R5 – The smallest save Sonic CD can keep in the Sega CD's backup RAM

MicroNut99, 26 Oct 2026: *"find a way to make the min save possible using the SegaCD's. If you have
to split them into more than one game save, if that work."*

This port is **backup-RAM only**: everything that lasts goes through the Sub-CPU's commands 56 / 57 /
58 / 59 into the Sega CD's **internal** backup RAM (`cd32x/sub_main.c`, file `SONICCD32X_`), which is
8 KB shared by every game the console has ever saved, allocated in **64-byte blocks** with a
directory of 16-byte entries at the end. Nothing here touches the cart or a RAM-cart page, so it is
untouched by an EverDrive launcher's wipe of banks 0–29 - and equally untouched by ED-SAVE, which
only moves the *cart* save area into bank 31.

## What the file holds today (R4C, `cd32x/engine/scd_save.h`)

| Words | Bytes | What |
|---|---|---|
| 0–2 | 6 | `'SC'`, version, checksum of words 3–255 |
| 3–38 | 72 | the four save files (slots 0–31; slots 2 and 6 of each are 32-bit) |
| 39–54 | 32 | options (slots 32–47) - of which only 32, 33, 34, 35, 36, 38, 39 were ever seen used |
| 55–180 | 252 | the time-attack table (slots 48–173): 7 zones × 3 acts × 3 records, each a time in hundredths **plus a second word beside it** |
| 181–255 | 150 | zero padding, inside the checksum |

**185 words = 370 bytes of data, declared as 8 blocks = 512 bytes.**

## The minimum, in order of how much has to be known first

**1. Free, no measurement needed: declare 6 blocks instead of 8.** The data ends at word 184. Set
`SCD_SAVE_WORDS` to 192 (6 × 64 = 384 bytes) and the checksum then covers words 3–191. Saves 128
bytes of the console's 8 KB. The format changes, so `scd_save_unpack` rejects an old file and the
slots keep their defaults - bump `SCD_SAVE_VERSION` so that rejection is deliberate rather than a
checksum accident.

**2. Measurable: drop what is provably unused.** Two candidates, both already observable with the
tool this layout was built from (`SCD32X_SAVELOG` in `host/plat_host.cpp`): play time attack, the
options and a save file on the PC build and log which slots ever take a non-zero value.
- the **second word of each time-attack record** - 63 words, 126 bytes. The R4C survey recorded "a
  word beside it" without a purpose. If it stays 0 through a set of records, drop it from the packing
  (pack 63 times, not 126 words) and restore 0 on unpack.
- the **nine option slots** never seen used (37, 40–47) - 9 words, 18 bytes, same test.
Both together: 185 − 72 = **113 words = 226 bytes → 4 blocks (256 bytes)**, half of today.
Do not guess these: a slot the game writes and we stop saving is a silent loss of a record.

## Splitting it: worth doing, but not to save space

Two files cost one extra 16-byte directory entry and no fewer data blocks, so splitting **cannot**
make the footprint smaller. It is worth doing for a different reason: **what must never fail should
be as small as possible and stand alone.**

| File | Blocks | Holds | If it is missing or corrupt |
|---|---|---|---|
| `SONICCD32X_` | **2 (128 B)** | header + the four save files + the options actually used (≈ 46 words = 92 bytes) | the player's progress is gone - this is the one that must work |
| `SONICCD32XT` | 4 (256 B) | header + the time-attack table | the records reset; progress is untouched |

The progress file then fits in the smallest allocation the BIOS can make, and a backup RAM that is
nearly full can still take it when the bigger one will not fit - which is the realistic failure on a
console whose 8 KB has other games' saves in it. `BRMWRITE` is called per file, so the game can also
write only the one that changed (time-attack records change rarely, progress often).

## What this does NOT fix, and the alternative if the internal RAM proves unreliable

The internal backup RAM has a poor record on this stack: OpenJazz moved off it (its saves are up to
~2 KB and it "did not work on the console"), and SuperTux's notes say plainly that backup RAM "never
worked on the console" (CD32X BootCamp `docs/07`). If Sonic's file turns out not to survive on the
console either, the route that is now proven is the one Hurrican was given on 26 Oct: in the
**EverDrive launcher's** Sub-CPU program only (`CART_EDSAVE`), send commands 56 / 59 to a save piece
in **bank 31** - the EverDrive's battery RAM, kept on the SD card as
`MEGA/gamedata/<launcher>.32X/bram.srm` - instead of the BIOS calls. Hurrican's
`cd32x/subcpu/main.c` does it in about 30 lines with no engine change and no change to the disc
build, using save piece 12; this port would need a piece of its own from the ledger. That is also
the only way these saves survive on a cart-booted launcher if the console's BRAM is full.

## Order of work

1. Bump to 6 blocks + a version bump (no measurement, no risk).
2. Run the `SCD32X_SAVELOG` pass for the two unused-slot questions; if confirmed, pack to 4 blocks.
3. Split into the 2-block progress file + 4-block time-attack file.
4. Only if the console shows the internal RAM failing: add the `CART_EDSAVE` route to bank 31.

Every step changes the file format, so do them in one version bump rather than four.
