/* PORT32X: Mega Drive plane streaming for the Sonic CD engine's tile layers.
 * Plain C, no host dependencies: the PC host build runs it against a model of the VDP,
 * and the 68000 will run the same file against the real VDP (round S3b).
 *
 * A plane is 64 x 32 cells (512 x 256 pixels). Each plane row holds one layer cell row
 * and a window of 64 consecutive layer columns; md_plane_update() writes only the cells
 * that enter a window, and fills the per-line horizontal scroll and the plane's vertical
 * scroll so the VDP shows exactly what the engine's DrawHLineScrollLayer() draws.
 * A layer whose width divides 512 pixels is repeated across the whole plane row instead
 * (any scroll then works, as in the classic Sonic backgrounds).
 * 68000 notes: no division or modulo per line, no 32-bit multiply (16x16 only); one
 * modulo per row that gets written. */
#ifndef MD_PLANES_H
#define MD_PLANES_H
#include <stdint.h>

#define MD_PLANE_W 64
#define MD_PLANE_H 32

/* chunk table entry: 16x16 tile | flip << 10 | in front (visualPlane 1) << 12 | never drawn << 13 */
#define MD_CE_TILE(e)   ((e) & 0x3FF)
#define MD_CE_FLIP(e)   (((e) >> 10) & 3)
#define MD_CE_FRONT(e)  (((e) >> 12) & 1)
#define MD_CE_HIDDEN(e) (((e) >> 13) & 1)

typedef struct {
    const uint16_t *chunks;  /* 512 chunks x 64 entries (MD_CE_*) */
    const uint16_t *quads;   /* [tile][flip][quadrant TL,TR,BL,BR] name words, priority bit clear */
    const uint16_t *layout;  /* chunk numbers, row by row, layoutStride entries per row */
    int layoutStride;
    int wChunks, hChunks;    /* layer size in 128-pixel chunks */
    uint8_t showBack, showFront; /* which tiles this plane shows (the engine's slots decide) */
} MdLayer;

typedef struct {
    int16_t rowLayer[MD_PLANE_H]; /* layer cell row held by each plane row, -1 = none */
    int32_t rowC0[MD_PLANE_H];    /* first layer column of the row's window (unwrapped) */
    int32_t rowK[MD_PLANE_H];     /* the row's unwrap offset (a multiple of the layer width) */
    uint32_t written;             /* name words written by the last update */
    uint32_t rowsFull;            /* rows written whole by the last update */
    uint32_t overflow;            /* rows whose lines needed more than 64 columns (last update) */
    uint32_t overflowRows;        /* bit r set: plane row r overflowed (last update) */
} MdPlane;

typedef void (*MdWriteFn)(void *ctx, uint16_t vramAddr, uint16_t word);

void md_plane_reset(MdPlane *p);
uint16_t md_name(const MdLayer *L, int cx, int cy); /* cx, cy: layer cell, already wrapped */

/* tileYPos: layer line shown on screen line 0 (0 <= tileYPos < layer height).
 * lineX[lines]: layer x shown at the left edge of each screen line (0 <= x < layer width).
 * Outputs hscroll[lines] (VDP horizontal scroll values) and *vscroll. */
void md_plane_update(MdPlane *p, const MdLayer *L, uint16_t nameBase, int tileYPos,
                     const int *lineX, int lines, int screenW,
                     int16_t *hscroll, uint16_t *vscroll, MdWriteFn write, void *ctx);

/* REV R4B: the layout entry of chunk (chunkX, chunkY) changed (a script's SetTileLayerEntry): the chunk's cells that
   the plane's rows hold now are written again (rows written later read the new layout themselves). */
void md_plane_chunk(MdPlane *p, const MdLayer *L, uint16_t nameBase, int chunkX, int chunkY, MdWriteFn write, void *ctx);
#endif
