/* PORT32X: see md_planes.h */
#include "md_planes.h"
#if (defined(__sh__) && !defined(MARS)) || defined(MD_SGB_RUNS)
#include "sgb.h"
#define MD_SGB_RUN 1 /* R1W: a row piece goes to the record writer in runs of consecutive addresses */
#else
#define MD_SGB_RUN 0
#endif
#ifndef MD_SLACK
#define MD_SLACK 2 /* R1W: columns the window keeps ahead of the lines when it has to move */
#endif
/* PORT32X R1V: code the SH2 runs every frame - in the engine build it goes into SDRAM (cd32x/engine/engine_cart.ld
   .sdtext; a code cache miss costs 11 cycles there, 87 in the cart). Not in the other builds (MARS: Kobo's SH2
   programs; the PC). */
#if defined(__sh__) && !defined(MARS)
#define SCD_MD_HOT __attribute__((section(".sdtext")))
#else
#define SCD_MD_HOT
#endif
void md_plane_update(MdPlane *p, const MdLayer *L, uint16_t nameBase, int tileYPos, const int *lineX, int lines, int screenW,
                     int16_t *hscroll, uint16_t *vscroll, MdWriteFn write, void *ctx) SCD_MD_HOT;

void md_plane_reset(MdPlane *p)
{
    int r;
    for (r = 0; r < MD_PLANE_H; r++) {
        p->rowLayer[r] = -1;
        p->rowC0[r]    = 0;
        p->rowK[r]     = 0;
    }
    p->written = p->rowsFull = p->overflow = p->overflowRows = 0;
}

uint16_t md_name(const MdLayer *L, int cx, int cy)
{
    /* 16 x 16 multiply only (the 68000 has no 32-bit multiply) */
    uint16_t chunk = L->layout[(int32_t)(int16_t)(cy >> 4) * (int16_t)L->layoutStride + (cx >> 4)];
    uint16_t e     = L->chunks[((uint32_t)chunk << 6) + (((cy >> 1) & 7) << 3) + ((cx >> 1) & 7)];
    int front      = MD_CE_FRONT(e);
    if (MD_CE_HIDDEN(e) || (front ? !L->showFront : !L->showBack))
        return 0;
    return (uint16_t)(L->quads[(((uint32_t)MD_CE_TILE(e) << 2) + MD_CE_FLIP(e)) * 4 + ((cy & 1) << 1) + (cx & 1)] | (front << 15));
}

/* write layer columns c0..c1 (unwrapped) of one layer cell row into plane row r */
static void write_cols(MdPlane *p, const MdLayer *L, uint16_t nameBase, int r, int layerRow, int32_t c0, int32_t c1,
                       int wCells, MdWriteFn write, void *ctx) SCD_MD_HOT;
static void write_cols(MdPlane *p, const MdLayer *L, uint16_t nameBase, int r, int layerRow, int32_t c0, int32_t c1,
                       int wCells, MdWriteFn write, void *ctx)
{
    /* R1W: md_name's reads, taken once per chunk (the layout entry) and once per 16-pixel tile (the chunk entry) along
       the row instead of once per cell - the same names; then the piece's words in runs of consecutive addresses
       (one at the plane's 64-column wrap). The names do not depend on the writes (the record is not the layout).
       R1X: one tile a step - both its cells (an even column), or one cell (an odd column: the tile's right one, or the
       piece's last column: its left one); the layer's width is whole chunks, so a tile never straddles its wrap. */
    uint16_t names[MD_PLANE_W];
    const uint16_t *lrow = L->layout + (int32_t)(int16_t)(layerRow >> 4) * (int16_t)L->layoutStride;
    const uint16_t *chunks = L->chunks, *quads = L->quads, *crow = 0;
    const unsigned ey = (unsigned)((layerRow >> 1) & 7) << 3, qy = (unsigned)(layerRow & 1) << 1;
    const int sf = L->showFront, sb = L->showBack;
    const uint16_t rowAddr = (uint16_t)(nameBase + ((r * MD_PLANE_W) << 1));
    int cwi = (int)(c0 % wCells); /* the only modulo: once per written row piece */
    unsigned cw, wc = (unsigned)wCells, lastChunk = ~0u, col = (unsigned)c0 & (MD_PLANE_W - 1);
    int32_t left = c1 - c0 + 1;
    if (cwi < 0)
        cwi += wCells;
    cw = (unsigned)cwi;
    while (left > 0) {
        int n = left > (int32_t)(MD_PLANE_W - col) ? (int)(MD_PLANE_W - col) : (int)left, k = 0;
        while (k < n) {
            uint16_t e, a = 0, b = 0;
            if ((cw >> 4) != lastChunk) {
                lastChunk = cw >> 4;
                crow      = chunks + ((uint32_t)lrow[lastChunk] << 6) + ey;
            }
            e = crow[(cw >> 1) & 7];
            if (!MD_CE_HIDDEN(e) && (MD_CE_FRONT(e) ? sf : sb)) {
                const uint16_t *q = quads + (((uint32_t)MD_CE_TILE(e) << 2) + MD_CE_FLIP(e)) * 4 + qy;
                uint16_t front    = (uint16_t)(MD_CE_FRONT(e) << 15);
                a                 = (uint16_t)(q[0] | front);
                b                 = (uint16_t)(q[1] | front);
            }
            if (!(cw & 1) && k + 1 < n) {
                names[k]     = a;
                names[k + 1] = b;
                k += 2;
                cw += 2;
            }
            else {
                names[k++] = (cw & 1) ? b : a;
                cw++;
            }
            if (cw == wc)
                cw = 0;
        }
#if MD_SGB_RUN
        if (write == sgb_write)
            sgb_write_run(ctx, (uint16_t)(rowAddr + (col << 1)), names, n);
        else
#endif
            for (k = 0; k < n; k++) write(ctx, (uint16_t)(rowAddr + ((col + (unsigned)k) << 1)), names[k]);
        p->written += (uint32_t)n;
        left -= n;
        col = 0;
    }
}

void md_plane_update(MdPlane *p, const MdLayer *L, uint16_t nameBase, int tileYPos,
                     const int *lineX, int lines, int screenW,
                     int16_t *hscroll, uint16_t *vscroll, MdWriteFn write, void *ctx)
{
    int W = L->wChunks << 7, wCells = L->wChunks << 4, hCells = L->hChunks << 4, halfW = W >> 1;
    int repeat   = (MD_PLANE_W % wCells) == 0; /* the layer repeats exactly across the plane */
    int y        = 0;
    int layerRow = tileYPos >> 3; /* 0 <= tileYPos < layer height, so no modulo needed */
    p->written = p->rowsFull = p->overflow = p->overflowRows = 0;
    *vscroll   = (uint16_t)(tileYPos & 0xFF);
    while (y < lines) {
        /* the screen lines that fall in one plane row */
        int lyU  = tileYPos + y;
        int r    = (lyU >> 3) & (MD_PLANE_H - 1);
        int yEnd = y + (8 - (lyU & 7));
        int k;
        if (yEnd > lines)
            yEnd = lines;
        if (repeat) {
            for (k = y; k < yEnd; k++)
                hscroll[k] = (int16_t)(-lineX[k] & 0x3FF);
            if (p->rowLayer[r] != layerRow) {
                write_cols(p, L, nameBase, r, layerRow, 0, MD_PLANE_W - 1, wCells, write, ctx);
                p->rowLayer[r] = (int16_t)layerRow;
                p->rowsFull++;
            }
        }
        else {
            int fresh = p->rowLayer[r] != layerRow;
            int32_t ref, xmin = 0x7FFFFFFF, xmax = -0x7FFFFFFF, a, b, need;
            int32_t kW;
            /* unwrap the lines' x near the row's current window (a fresh row starts at its first line) */
            if (fresh) {
                ref = lineX[y];
                kW  = 0;
            }
            else {
                ref = (p->rowC0[r] << 3) + (MD_PLANE_W << 2);
                kW  = p->rowK[r];
            }
            for (k = y; k < yEnd; k++) {
                int32_t x;
                /* R1X: a line with the previous line's x has its x, its scroll and no new min / max (the previous
                   line left x within halfW of ref: no step below) */
                if (k > y && lineX[k] == lineX[k - 1]) {
                    hscroll[k] = hscroll[k - 1];
                    continue;
                }
                x = lineX[k] + kW;
                /* normally no or one step; a camera jump takes a few */
                while (x - ref > halfW) {
                    x -= W;
                    kW -= W;
                }
                while (ref - x > halfW) {
                    x += W;
                    kW += W;
                }
                hscroll[k] = (int16_t)(-x & 0x3FF);
                if (x < xmin)
                    xmin = x;
                if (x > xmax)
                    xmax = x;
            }
            p->rowK[r] = kW;
            a          = xmin >> 3; /* arithmetic shift: floor for negatives too */
            b          = (xmax + screenW - 1) >> 3;
            need       = b - a + 1;
            if (need > MD_PLANE_W) {
                p->overflow++;
                p->overflowRows |= 1u << r;
                b    = a + MD_PLANE_W - 1;
                need = MD_PLANE_W;
            }
            if (fresh) {
                int32_t c0 = a - (MD_PLANE_W - need) / 2;
                write_cols(p, L, nameBase, r, layerRow, c0, c0 + MD_PLANE_W - 1, wCells, write, ctx);
                p->rowLayer[r] = (int16_t)layerRow;
                p->rowC0[r]    = c0;
                p->rowsFull++;
            }
            else {
                int32_t c0 = p->rowC0[r];
                if (a < c0 || b > c0 + MD_PLANE_W - 1) {
                    /* R1W: the window moves only MD_SLACK columns past the lines' columns (not to centre them): the
                       same picture, its new columns written a few at a time as the camera moves instead of a third
                       of the plane in one frame (that frame's record and its copy by the 68000 were the longest) */
                    int32_t m  = (MD_PLANE_W - need) / 2 < MD_SLACK ? (MD_PLANE_W - need) / 2 : MD_SLACK;
                    int32_t n0 = a < c0 ? a - m : b - (MD_PLANE_W - 1) + m;
                    if (n0 <= c0 - MD_PLANE_W || n0 >= c0 + MD_PLANE_W) {
                        write_cols(p, L, nameBase, r, layerRow, n0, n0 + MD_PLANE_W - 1, wCells, write, ctx);
                        p->rowsFull++;
                    }
                    else if (n0 < c0)
                        write_cols(p, L, nameBase, r, layerRow, n0, c0 - 1, wCells, write, ctx);
                    else
                        write_cols(p, L, nameBase, r, layerRow, c0 + MD_PLANE_W, n0 + MD_PLANE_W - 1, wCells, write, ctx);
                    p->rowC0[r] = n0;
                }
            }
        }
        y = yEnd;
        if (++layerRow == hCells)
            layerRow = 0;
    }
}

/* REV R4B: see md_planes.h. A row holds the layer's columns rowC0 .. rowC0 + 63 (unwrapped: plane column = column & 63,
   layer column = column modulo the layer's width), or for a layer that repeats across the plane its columns 0 .. 63 */
void md_plane_chunk(MdPlane *p, const MdLayer *L, uint16_t nameBase, int chunkX, int chunkY, MdWriteFn write, void *ctx)
{
    int wCells = L->wChunks << 4, repeat = (MD_PLANE_W % wCells) == 0, r, k;
    for (r = 0; r < MD_PLANE_H; r++) {
        int lr = p->rowLayer[r];
        if (lr < 0 || (lr >> 4) != chunkY)
            continue;
        for (k = 0; k < 16; k++) {
            int w      = (chunkX << 4) + k;
            int32_t c0 = repeat ? 0 : p->rowC0[r], d = (w - c0) % wCells, u;
            if (d < 0)
                d += wCells;
            for (u = c0 + d; u <= c0 + MD_PLANE_W - 1; u += wCells) {
                write(ctx, (uint16_t)(nameBase + ((r * MD_PLANE_W + (int)(u & (MD_PLANE_W - 1))) << 1)), md_name(L, w, lr));
                p->written++;
            }
        }
    }
}
