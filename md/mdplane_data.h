/* PORT32X: the data part of MDPLANE.BIN (written by host/md_check.cpp, SCD32X_MDEXPORT).
 * All values big-endian (the 68000's own order), 16-bit aligned.
 *
 * header (72 bytes)
 *   0  'SCMD'          4  u16 version (1)     6  u16 lines (224)    8  u16 screen width (320)
 *  10  u16 frames     12  u32 cells offset    16  u16 cell count (cell 0 = blank)
 *  20  u32 quads offset (16 name words per compact tile: 4 flips x 4 quadrants)
 *  24  u32 chunk table offset (64 entries per compact chunk, MD_CE_* format, compact tiles)
 *  28  u32 CRAM offset (64 words, MD colour format 0000BBB0GGG0RRR0)
 *  32  u32 frames offset
 *  36  plane A: u32 layout offset, u16 width, u16 height (chunks), u8 show behind, u8 show front, u16 0
 *  48  plane B: same (width 0 = no background plane)
 *  60  u32 total size  64  u16 status palette line (0xFFFF = none)  66  u16 status slot  68  u32 0
 * frame (repeated) - FORMAT 2 (S3c): everything precomputed on the PC, the 68000 only copies
 *   u16 V scroll A, u16 V scroll B
 *   H scroll A: u16 runs, runs x (u16 lines, u16 VDP value); H scroll B: the same
 *   u16 groups, groups x (u16 VRAM address, u16 count, count x u16 name word) - consecutive addresses
 *   u16 palette changes, changes x (u16 CRAM index 0..63, u16 colour)
 * (format 2 header: quads / chunk / layout offsets unused; 52 = u16 plane B used)
 */
#ifndef MDPLANE_DATA_H
#define MDPLANE_DATA_H
#define MDP_MAGIC 0x53434D44u /* 'SCMD' */
#define MDP_STATUS_CELL 1520  /* 16 hex digit cells at VRAM 0xBE00 */
/* S4 live mode: record numbers in COMM8 / COMM10 - never 0 (power-on) or 0xDA7A (the start signal) */
#define MDP_DATA_READY 0xDA7A
#define MDP_NEXT_ID(x) ((x) == 0xFFFF ? 1 : (x) + 1 == MDP_DATA_READY ? MDP_DATA_READY + 1 : (x) + 1)
#endif
