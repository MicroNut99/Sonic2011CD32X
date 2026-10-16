# R0 – every act through the Mega Drive plane model (PC only)

Question: can Sonic CD (2011)'s levels be "ripped and torn" onto the Genesis planes, the way
Jazz Jackrabbit CD32X REV and SuperTux CD32X REV do it?

Method: `tools/survey_md.sh Data.rsdk 1800` - each regular act runs 1,800 frames (30 s) in the
host engine with the spin-dash pad; `host/md_check.cpp` (round S3a) converts the act from the
engine's own arrays into Mega Drive data (8x8 cells deduplicated over flips, palette lines in
9-bit colour, name words per 16x16 tile, front/behind priority), streams the planes with
`md/md_planes.c` (64 x 32 planes, per-line h-scroll) and compares a VDP model of every frame with
the engine's own tile layers, pixel by pixel. Sprites are not part of the check (they go to the 32X).

## Result

- **68 of 70 acts converted.** R51B and R52B did not start from the dev menu in this run (0
  frames compared) - to look at, not a sign of a problem with their data.
- **Video memory: 332-801 cells per act** (10-25 KB). The S3 VRAM plan has room for 1,535.
  Every act fits, with room to spare for the Genesis-side extras.
- **Palette lines for the tiles: 2-4** (of the Genesis's 4). Sprites stay on the 32X (256 colours).
- **0 unsupported layers** (no vertical-scroll-per-column, 3D or double backgrounds in any act) and
  **0 rows the VDP cannot show** anywhere: every act's layers are two Genesis planes with per-line
  horizontal scroll, as the 1993 game had.
- **46 acts identical** to the engine's layers in every compared frame. 22 acts have differing
  frames - small local spots (a 16 x 16 tile, a part of a loop, a few pixels; see below), not
  whole-screen errors. Each needs its cause found: R11C/D, R12B-D, R71B-D have layout changes
  in the run (a broken wall, a moved block); R6x/R8x have many animated tiles.
- **Animated tiles are the one real design item.** In R5xD, R6x and R8x the model re-writes name
  words for animated tiles: 360-1,376 words a frame on average. That is too much for one
  vblank through the 68000. The Genesis way is to animate the *patterns* (rewrite the few
  animated cells in VRAM) and leave the map alone - the converter has to group each animation's
  frames into fixed cells. This is the main task of round R2.

## Per act

Columns: palette lines (tiles), cells (8x8 patterns in VRAM), frames compared, frames that
differ, unsupported layers, name words a frame on average (plane A / B), rows the VDP cannot
show, animated-tile updates / layout changes in the run.

| # | Act | Lines | Cells | Compared | Differ | Unsupp. | Words A / B | Bad rows | Anim / layout |
|---|---|---|---|---|---|---|---|---|---|
| 0 | R11A/1 | 3 | 740 | 1799 | 0 | 0 | 25 / 16 | 0 | 0 / 0 |
| 1 | R11B/1 | 3 | 720 | 1799 | 0 | 0 | 17 / 3 | 0 | 0 / 0 |
| 2 | R11C/1 | 3 | 738 | 1799 | 4 | 0 | 17 / 4 | 0 | 0 / 1 |
| 3 | R11D/1 | 3 | 747 | 1799 | 4 | 0 | 18 / 4 | 0 | 0 / 1 |
| 4 | R12A/2 | 3 | 801 | 1799 | 0 | 0 | 15 / 2 | 0 | 0 / 0 |
| 5 | R12B/2 | 3 | 764 | 1799 | 43 | 0 | 15 / 3 | 0 | 0 / 1 |
| 6 | R12C/2 | 3 | 766 | 1799 | 48 | 0 | 34 / 8 | 0 | 0 / 2 |
| 7 | R12D/2 | 3 | 786 | 1799 | 91 | 0 | 21 / 4 | 0 | 0 / 1 |
| 8 | R13C/3 | 3 | 637 | 1799 | 0 | 0 | 11 / 2 | 0 | 0 / 0 |
| 9 | R13D/3 | 3 | 630 | 1798 | 0 | 0 | 7 / 1 | 0 | 0 / 0 |
| 10 | R31A/1 | 3 | 656 | 1797 | 0 | 0 | 9 / 2 | 0 | 0 / 0 |
| 11 | R31B/1 | 2 | 643 | 1799 | 0 | 0 | 11 / 3 | 0 | 0 / 0 |
| 12 | R31C/1 | 2 | 627 | 1799 | 0 | 0 | 23 / 8 | 0 | 0 / 0 |
| 13 | R31D/1 | 3 | 636 | 1799 | 0 | 0 | 24 / 8 | 0 | 0 / 0 |
| 14 | R32A/2 | 3 | 616 | 1799 | 0 | 0 | 19 / 6 | 0 | 0 / 0 |
| 15 | R32B/2 | 2 | 604 | 1799 | 328 | 0 | 25 / 6 | 0 | 0 / 0 |
| 16 | R32C/2 | 2 | 597 | 1799 | 261 | 0 | 20 / 6 | 0 | 0 / 0 |
| 17 | R32D/2 | 3 | 602 | 1799 | 261 | 0 | 22 / 6 | 0 | 0 / 0 |
| 18 | R33C/3 | 3 | 558 | 1799 | 0 | 0 | 6 / 2 | 0 | 0 / 0 |
| 19 | R33D/3 | 3 | 562 | 1799 | 0 | 0 | 6 / 2 | 0 | 0 / 0 |
| 20 | R41A/1 | 3 | 511 | 1799 | 0 | 0 | 28 / 26 | 0 | 21 / 0 |
| 21 | R41B/1 | 2 | 452 | 1799 | 0 | 0 | 7 / 2 | 0 | 0 / 0 |
| 22 | R41C/1 | 2 | 441 | 1799 | 0 | 0 | 7 / 2 | 0 | 0 / 0 |
| 23 | R41D/1 | 2 | 505 | 1799 | 0 | 0 | 7 / 2 | 0 | 0 / 0 |
| 24 | R42A/2 | 2 | 438 | 1799 | 0 | 0 | 6 / 1 | 0 | 0 / 0 |
| 25 | R42B/2 | 2 | 455 | 1799 | 0 | 0 | 5 / 1 | 0 | 0 / 0 |
| 26 | R42C/2 | 2 | 439 | 1799 | 0 | 0 | 5 / 1 | 0 | 0 / 0 |
| 27 | R42D/2 | 2 | 484 | 1799 | 0 | 0 | 5 / 1 | 0 | 0 / 0 |
| 28 | R43C/3 | 2 | 332 | 1799 | 0 | 0 | 8 / 2 | 0 | 0 / 0 |
| 29 | R43D/3 | 2 | 389 | 1799 | 0 | 0 | 8 / 2 | 0 | 0 / 0 |
| 30 | R51A/1 | 4 | 701 | 1799 | 1048 | 0 | 6 / 3 | 0 | 0 / 0 |
| 31 | (did not load) | | | | | | | | |
| 32 | R51C/1 | 4 | 669 | 1799 | 0 | 0 | 9 / 1 | 0 | 0 / 0 |
| 33 | R51D/1 | 4 | 651 | 1799 | 0 | 0 | 464 / 464 | 0 | 449 / 0 |
| 34 | R52A/2 | 4 | 709 | 1799 | 594 | 0 | 19 / 8 | 0 | 0 / 0 |
| 35 | (did not load) | | | | | | | | |
| 36 | R52C/2 | 4 | 674 | 1799 | 0 | 0 | 10 / 1 | 0 | 0 / 0 |
| 37 | R52D/2 | 4 | 681 | 1799 | 0 | 0 | 468 / 464 | 0 | 449 / 0 |
| 38 | R53C/3 | 2 | 436 | 1799 | 0 | 0 | 8 / 1 | 0 | 0 / 0 |
| 39 | R53D/3 | 2 | 360 | 1799 | 0 | 0 | 15 / 2 | 0 | 0 / 0 |
| 40 | R61A/1 | 3 | 524 | 1799 | 955 | 0 | 1357 / 1366 | 0 | 1325 / 0 |
| 41 | R61B/1 | 3 | 574 | 1799 | 577 | 0 | 1351 / 1367 | 0 | 1325 / 0 |
| 42 | R61C/1 | 3 | 542 | 1799 | 0 | 0 | 965 / 978 | 0 | 947 / 0 |
| 43 | R61D/1 | 3 | 633 | 1799 | 624 | 0 | 1179 / 1195 | 0 | 1158 / 0 |
| 44 | R62A/2 | 3 | 652 | 1799 | 286 | 0 | 1376 / 1367 | 0 | 1325 / 0 |
| 45 | R62B/2 | 3 | 612 | 1799 | 175 | 0 | 1347 / 1364 | 0 | 1325 / 0 |
| 46 | R62C/2 | 3 | 644 | 1799 | 0 | 0 | 994 / 987 | 0 | 955 / 0 |
| 47 | R62D/2 | 3 | 695 | 1799 | 195 | 0 | 1182 / 1190 | 0 | 1158 / 0 |
| 48 | R63C/3 | 2 | 452 | 1799 | 276 | 0 | 932 / 926 | 0 | 899 / 0 |
| 49 | R63D/3 | 3 | 544 | 1799 | 360 | 0 | 1197 / 1191 | 0 | 1158 / 0 |
| 50 | R71A/1 | 3 | 432 | 1798 | 0 | 0 | 21 / 10 | 0 | 0 / 3 |
| 51 | R71B/1 | 3 | 551 | 1798 | 216 | 0 | 14 / 6 | 0 | 0 / 1 |
| 52 | R71C/1 | 3 | 634 | 1798 | 216 | 0 | 15 / 3 | 0 | 0 / 1 |
| 53 | R71D/1 | 3 | 515 | 1798 | 216 | 0 | 15 / 7 | 0 | 0 / 1 |
| 54 | R72A/2 | 3 | 715 | 1798 | 0 | 0 | 14 / 3 | 0 | 0 / 0 |
| 55 | R72B/2 | 3 | 727 | 1799 | 0 | 0 | 15 / 4 | 0 | 0 / 1 |
| 56 | R72C/2 | 3 | 733 | 1799 | 0 | 0 | 53 / 13 | 0 | 0 / 5 |
| 57 | R72D/2 | 3 | 704 | 1799 | 0 | 0 | 50 / 17 | 0 | 0 / 5 |
| 58 | R73C/3 | 4 | 572 | 1799 | 0 | 0 | 10 / 6 | 0 | 0 / 0 |
| 59 | R73D/3 | 3 | 492 | 1799 | 0 | 0 | 10 / 7 | 0 | 0 / 0 |
| 60 | R81A/1 | 4 | 492 | 1799 | 0 | 0 | 999 / 992 | 0 | 977 / 0 |
| 61 | R81B/1 | 3 | 503 | 1799 | 0 | 0 | 3 / 1 | 0 | 0 / 0 |
| 62 | R81C/1 | 3 | 529 | 1799 | 0 | 0 | 892 / 885 | 0 | 872 / 0 |
| 63 | R81D/1 | 3 | 550 | 1799 | 0 | 0 | 892 / 885 | 0 | 872 / 0 |
| 64 | R82A/2 | 3 | 518 | 1797 | 384 | 0 | 1131 / 1138 | 0 | 1102 / 0 |
| 65 | R82B/2 | 3 | 533 | 1799 | 0 | 0 | 364 / 371 | 0 | 359 / 0 |
| 66 | R82C/2 | 3 | 516 | 1797 | 0 | 0 | 1124 / 1139 | 0 | 1103 / 0 |
| 67 | R82D/2 | 4 | 546 | 1797 | 0 | 0 | 1124 / 1139 | 0 | 1103 / 0 |
| 68 | R83C/3 | 3 | 497 | 1799 | 0 | 0 | 1116 / 1138 | 0 | 1103 / 0 |
| 69 | R83D/3 | 3 | 425 | 1799 | 0 | 0 | 1091 / 1112 | 0 | 1078 / 0 |

Frames that differ are saved by the check as `runs/scene<N>/frames/mdcheck_<frame>.png`
(reference | model | difference).
