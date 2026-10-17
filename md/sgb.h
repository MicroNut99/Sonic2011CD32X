/* REV R1: the Genesis planes driven by the engine on the SH2 ("SGB" = Sonic Genesis background).
 *
 * Each frame the SH2 streams both planes with md_planes.c (the code S3a proved against the engine,
 * frame for frame) and writes the frame's VDP work as a RECORD straight into the 32X frame buffer's
 * record area; the 68000 copies it into its work RAM (listener command loop) and into the VDP in
 * vertical blank (cd32x/kobo/hw_md.s, sgb_poll / sgb_vblank).
 *
 * Record (16-bit words, the SH2's and the 68000's own order - big-endian):
 *   [0]  length: words after this one
 *   [1]  G = name-table write groups; G x { VRAM address, count n, n name words } (consecutive addresses)
 *        vertical scroll plane A, plane B
 *        h-scroll plane A: runs R, R x { lines, VDP value }; plane B the same
 *        palette changes P, P x { CRAM index 0-63, Genesis colour 0000BBB0GGG0RRR0 }
 *   REV R4D (the Options Menu's UNDERWATER switch on - off: no trailer, the R4C record): the water trailer at the end,
 *        Q x { CRAM index 0-63, water colour }, Q, W = the first line drawn with the water colours (0: none - the
 *        act has no water line this frame), SGB_WATER_TAG (the last word: an R4C record ends with a colour <= 0x0EEE or
 *        P = 0). The 68000 keeps the 64 water colours (the changes since the last trailer) and the 64 normal ones (the
 *        palette changes, a shadow of CRAM); at vertical blank the normal ones go back into CRAM, at line W - 1's end
 *        its H-int writes the water ones (cd32x/kobo/hw_md.s sgb_water / hint_int). A record without the trailer: no
 *        water line.
 *
 * The act's Genesis data ("SCMG", built on the PC by host/md_check.cpp, SCD32X_MDGEXPORT), big-endian:
 *   0 'SCMG'   4 u16 version (1)   6 u16 cells (cell 0 blank)   8 u32 total size
 *  12 u32 cells offset (32 bytes a cell)
 *  16 u32 palette slots offset: 64 x u16 engine palette index shown in CRAM slot k (0xFFFF = none)
 *  20 u32 quads offset: 1024 tiles x 4 flips x 4 quadrants name words (engine tile numbers)
 *  24 u32 chunks offset: 512 chunks x 64 entries (MD_CE_* format, md_planes.h; engine chunk numbers)
 *  28 u32 zero block offset (4096 zero words: the 68000 clears the planes from it)
 *  32 u32 CRAM offset (64 Genesis colours at conversion time)
 *  36 char[12] the stage ("folder/id"; R3: 56-63 the sprite pack, md_sprites.h)
 *  48 u32 'SKY1' (REV R4B) 52 u32 the sky section's offset: u16 background layer, sky colour (engine palette index),
 *     rows (the layer's height), pairs (rows / 2), the sky colour's RGB565 when the file was made, 0; then pairs x
 *     { u16 A, u16 B }: the 32X colours (0BBBBBGGGGGRRRRR) of the background's row pair, a 2 x 2 checkerboard of A and B
 * REV R4C: the file's last 8 bytes u32 offset, u32 'ANI1': the act's animated tiles (Copy16x16Tile) - at the offset
 *   128 bytes: the tiles with name words (bit t of byte t >> 3; the quads hold every picture the act's animations show,
 *   as loaded), 64 bytes: the chunks that hold an animated tile (bit c of byte c >> 3); the SH2 shows tile t with the
 *   name words of the tile whose pixels it has (md_planes.h tileMap, md_tile_copy, md_plane_refresh)
 * Plain C, no host dependencies. */
#ifndef SGB_H
#define SGB_H
#include <stdint.h>
#include "md_planes.h"

#define SGB_MAGIC 0x53434D47u /* 'SCMG' */
#define SGB_VERSION 1
#define SGB_HEADER 64
#define SGB_REC_MAX 6000      /* words: the frame buffer's 12 KB record area (a full reload is ~4,300) */
#define SGB_SKY_TAG 0x534B5931u /* 'SKY1': the act has a 32X sky (header words 48 / 52) */
#define SGB_ANI_TAG 0x414E4931u /* 'ANI1': the act has animated tiles (the file's last 8 bytes, see above) */
#define SGB_WATER_TAG 0x5741    /* REV R4D: 'WA' - the record's last word: the water trailer before it */
#define SGB_NAME_A 0xC000
#define SGB_NAME_B 0xE000

typedef struct {
    uint16_t *out;            /* record words */
    int pos, max;
    int groups, cntAt, cnt;
    uint16_t last;
    int overflow;
} SgbOut;

void sgb_out_begin(SgbOut *o, uint16_t *out, int max);
void sgb_write(void *ctx, uint16_t addr, uint16_t word); /* MdWriteFn, ctx = SgbOut* */
/* R1W: n words for the addresses addr, addr + 2, ... - exactly what n sgb_write calls write (md_planes.c's rows) */
void sgb_write_run(void *ctx, uint16_t addr, const uint16_t *words, int n);
/* closes the record; returns its size in words (the length word included), -1 if it did not fit */
int sgb_out_end(SgbOut *o, uint16_t vA, uint16_t vB, const int16_t *hsA, const int16_t *hsB, int lines,
                const uint16_t *cramIdx, const uint16_t *cramCol, int ncram);
/* REV R4D: after sgb_out_end - the water trailer (line 0: none; n water colour changes); the record's new size in words,
   -1 if it did not fit */
int sgb_out_water(SgbOut *o, int line, const uint16_t *idx, const uint16_t *col, int n);

/* engine colour (RGB565) -> Genesis CRAM word, as host/md_check.cpp's md_color */
uint16_t sgb_md_color(uint16_t rgb565);
/* REV R3: the engine's fade (DrawStageGFX's whole-screen rectangle, fadeMode) applied to a colour (RGB565) instead:
   the same mix per channel, done on the palette - the planes and the sprites fade with it */
uint16_t sgb_fade565(uint16_t c, int r, int g, int b, int a);

/* PC model of the 68000 side (the test): applies one record. vram_w(ctx, addr, word) for name words,
   hsA/hsB[lines] and vA/vB get the scroll, cram[64] the colour changes. Returns words consumed.
   REV R4D: wline / wcram (may be 0): the water trailer's line (0 without one) and its colour changes */
typedef void (*SgbVramFn)(void *ctx, uint16_t addr, uint16_t word);
int sgb_apply(const uint16_t *rec, int lines, SgbVramFn vram_w, void *ctx, int16_t *hsA, int16_t *hsB, uint16_t *vA,
              uint16_t *vB, uint16_t *cram, int *wline, uint16_t *wcram);
#endif
