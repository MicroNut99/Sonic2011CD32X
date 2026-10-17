/* REV R3: see md_sprites.h. Compiled inside cd32x/engine/plat_mars.cpp and host/md_check.cpp (extern "C", included):
   no Makefile change for the new file. */
#include "md_sprites.h"
#include "sgb.h"
/* the code the SH2 runs for every draw call goes into SDRAM in the engine build (as md_planes.c / sgb.c) */
#ifndef SCD_MD_HOT
#if defined(__sh__) && !defined(MARS)
#define SCD_MD_HOT __attribute__((section(".sdtext")))
#else
#define SCD_MD_HOT
#endif
#endif

#define F_SIZE 24
#define S_SIZE 12
#define P_SIZE 4
#define R_SIZE 96

uint16_t mds_rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t mds_rd32(const uint8_t *p) { return (uint32_t)mds_rd16(p) << 16 | mds_rd16(p + 2); }
/* the pack's fields are aligned: the SH2 (big-endian, like the pack) reads them as they are */
#if defined(__sh__)
#define RD16(p) (*(const uint16_t *)(p))
#define RD32(p) (*(const uint32_t *)(p))
#else
#define RD16(p) mds_rd16(p)
#define RD32(p) mds_rd32(p)
#endif

int mds_find(const MdSprites *s, int sheet, int sx, int sy, int w, int h) SCD_MD_HOT;
void mds_sprite(MdSprites *s, int kind, int sheet, int sx, int sy, int w, int h, int x, int y, int dir, int rot, int px, int py,
                int high) SCD_MD_HOT;

uint16_t mds_hash(int sheet, int sx, int sy, int w, int h)
{
    uint32_t k = (uint32_t)sheet * 0x9E3779B1u ^ (uint32_t)sx * 0x85EBCA77u ^ (uint32_t)sy * 0xC2B2AE3Du ^
                 (uint32_t)w * 0x27D4EB2Fu ^ (uint32_t)h * 0x165667B1u;
    k ^= k >> 15;
    k *= 0x2C1B3C6Du;
    k ^= k >> 12;
    return (uint16_t)k;
}

int mds_init(MdSprites *s, const uint8_t *p, uint16_t cellLo, uint16_t cellHi)
{
    int i;
    s->pack  = 0;
    s->ndraw = 0;
    s->nres  = 0;
    s->frame = 0;
    s->uploadWords = s->dropped = s->hwMax = s->evicted = 0;
    for (i = 0; i < MDS_MAX_SETS; i++) s->resOf[i] = 0;
    for (i = 0; i < MDS_CACHE; i++) s->cache[i].k2 = 0; /* no draw has a 0 x 0 rect */
    if (!p || mds_rd32(p) != MDS_MAGIC || mds_rd16(p + 4) != MDS_VERSION)
        return 0;
    s->nframes  = mds_rd16(p + 6);
    s->nsets    = mds_rd16(p + 8);
    s->hashBits = mds_rd16(p + 10);
    if (s->nsets > MDS_MAX_SETS || s->hashBits > 15)
        return 0;
    s->pack   = p;
    s->cellLo = cellLo;
    s->cellHi = cellHi;
    return 1;
}

int mds_find(const MdSprites *s, int sheet, int sx, int sy, int w, int h)
{
    const uint8_t *p = s->pack;
    if (!p)
        return -1;
    const uint8_t *hash = p + RD32(p + 16), *frames = p + RD32(p + 12);
    unsigned mask = (1u << s->hashBits) - 1, i = mds_hash(sheet, sx, sy, w, h) & mask, n;
    for (n = 0; n <= mask; n++, i = (i + 1) & mask) {
        unsigned e = RD16(hash + 2 * i);
        if (!e)
            return -1;
        const uint8_t *f = frames + F_SIZE * (e - 1);
        if (f[0] == sheet && RD16(f + 2) == sx && RD16(f + 4) == sy && RD16(f + 6) == w && RD16(f + 8) == h)
            return (int)e - 1;
    }
    return -1;
}

/* an engine draw call -> the frame's draw (the pack's tables are in the cart: a small cache of the last draws' frames) */
void mds_sprite(MdSprites *s, int kind, int sheet, int sx, int sy, int w, int h, int x, int y, int dir, int rot, int px, int py, int high)
{
    const uint8_t *p = s->pack;
    MdsCache tmp, *c = &tmp;
    int set, X, Y, flags = 0, cw, ch;
    if (!p || w <= 0 || h <= 0)
        return;
    if (kind == 2) { /* see-through (the engine's blended draws): every other frame, as the Genesis games did it */
        if (s->frame & 1)
            return;
        kind = 0;
    }
    if (s->ndraw >= MDS_MAX_DRAWS) {
        s->dropped++;
        return;
    }
    if ((unsigned)sx < 256 && (unsigned)sy < 256 && w < 256 && h < 256 && (unsigned)sheet < 256) {
        uint32_t k1 = (uint32_t)sheet << 16 | (uint32_t)sx << 8 | (uint32_t)sy;
        uint16_t k2 = (uint16_t)(w << 8 | h);
        c           = &s->cache[(k1 ^ (k1 >> 9) ^ (uint32_t)k2 * 7u) & (MDS_CACHE - 1)];
        if (c->k1 != k1 || c->k2 != k2) {
            int f = mds_find(s, sheet, sx, sy, w, h);
            c->k1 = k1;
            c->k2 = k2;
            c->frame = (uint16_t)(f < 0 ? 0xFFFF : f);
            if (f >= 0) {
                const uint8_t *fr = p + RD32(p + 12) + F_SIZE * f;
                c->set   = RD16(fr + 10);
                c->bx    = fr[12];
                c->by    = fr[13];
                c->cw    = fr[14];
                c->ch    = fr[15];
                c->flags = fr[1];
            }
        }
    }
    else { /* off the cache's key range: looked up each time */
        int f    = mds_find(s, sheet, sx, sy, w, h);
        c->frame = (uint16_t)(f < 0 ? 0xFFFF : f);
        if (f >= 0) {
            const uint8_t *fr = p + RD32(p + 12) + F_SIZE * f;
            c->set = RD16(fr + 10), c->bx = fr[12], c->by = fr[13], c->cw = fr[14], c->ch = fr[15], c->flags = fr[1];
        }
    }
    if (c->frame == 0xFFFF)
        return;
    set = c->set;
    cw  = c->cw;
    ch  = c->ch;
    if (kind == 1 && (c->flags & 1)) { /* the engine's own rotation, measured on the PC */
        const uint8_t *fr = p + RD32(p + 12) + F_SIZE * c->frame;
        int q             = ((rot & 0x1FF) + 0x40) >> 7 & 3;
        const uint8_t *r  = p + RD32(p + 28) + R_SIZE * RD16(fr + 16) + 12 * ((dir == 1) * 4 + q);
        int dx = px - (int16_t)RD16(fr + 18), dy = py - (int16_t)RD16(fr + 20);
        const uint8_t *st;
        set   = RD16(r);
        flags = r[2] & 3;
        X     = x + (int16_t)RD16(r + 4) + (int8_t)r[8] * dx + (int8_t)r[10] * dy;
        Y     = y + (int16_t)RD16(r + 6) + (int8_t)r[9] * dx + (int8_t)r[11] * dy;
        st    = p + RD32(p + 20) + S_SIZE * set;
        cw    = st[10];
        ch    = st[11];
    }
    else if (kind == 1) { /* a rotated draw of a frame the PC never saw rotated: as rotation 0 */
        if (dir == 1) {
            X     = x + px - c->bx - cw * 8;
            flags = MDS_HFLIP;
        }
        else
            X = x - px + c->bx;
        Y = y - py + c->by;
    }
    else {
        flags = dir & 3;
        X     = x + ((flags & MDS_HFLIP) ? w - c->bx - cw * 8 : c->bx);
        Y     = y + ((flags & MDS_VFLIP) ? h - c->by - ch * 8 : c->by);
    }
    if (!cw || X >= 320 || Y >= 224 || X + cw * 8 <= 0 || Y + ch * 8 <= 0)
        return; /* empty, or off the screen */
    MdsDraw *d = &s->draw[s->ndraw++];
    d->set     = (uint16_t)set;
    d->x       = (int16_t)X;
    d->y       = (int16_t)Y;
    d->flags   = (uint8_t)(flags | (high ? MDS_HIGH : 0));
}

void mds_forget(MdSprites *s)
{
    int i;
    for (i = 0; i < s->nres; i++) s->resOf[s->res[i].set] = 0;
    s->nres  = 0;
    s->ndraw = 0;
}

static int res_insert(MdSprites *s, int i, int base, int set, int n)
{
    int k;
    for (k = s->nres; k > i; k--) {
        s->res[k]               = s->res[k - 1];
        s->resOf[s->res[k].set] = (uint8_t)(k + 1);
    }
    s->nres++;
    s->res[i].set  = (uint16_t)set;
    s->res[i].base = (uint16_t)base;
    s->res[i].n    = (uint16_t)n;
    s->res[i].used = s->frame;
    s->resOf[set]  = (uint8_t)(i + 1);
    return i;
}
/* first fit in VRAM. If no gap takes the set: out goes the run of neighbouring sets (none of them drawn this frame)
   that, with the gaps around it, takes the set and whose newest member was drawn the longest ago (then the shortest
   run) - one pass over the sets (R3A: the least recently used set one at a time, with a whole new search after
   each, took up to 0.23 vertical blanks when 22 had to go). Returns the res index or -1. */
static int res_alloc(MdSprites *s, int set, int n)
{
    int i, a, b, k, prev = s->cellLo, bestA = -1, bestB = 0, bestAge = -1;
    if (s->nres < MDS_MAX_RES)
        for (i = 0; i <= s->nres; i++) {
            int end = i < s->nres ? s->res[i].base : s->cellHi;
            if (end - prev >= n)
                return res_insert(s, i, prev, set, n);
            if (i < s->nres)
                prev = s->res[i].base + s->res[i].n;
        }
    for (a = 0, b = 0; a < s->nres; a++) {
        int left = a ? s->res[a - 1].base + s->res[a - 1].n : s->cellLo, age;
        if (s->res[a].used == s->frame)
            continue; /* drawn this frame: no run through it */
        if (b < a + 1)
            b = a + 1; /* the run is res[a .. b - 1]; it frees from left to the next set's base */
        while ((b < s->nres ? s->res[b].base : s->cellHi) - left < n && b < s->nres && s->res[b].used != s->frame) b++;
        if ((b < s->nres ? s->res[b].base : s->cellHi) - left < n)
            continue; /* blocked by a set drawn this frame (or the end): a run starting further on */
        for (age = 0xFFFF, k = a; k < b; k++)
            if ((uint16_t)(s->frame - s->res[k].used) < age)
                age = (uint16_t)(s->frame - s->res[k].used);
        if (age > bestAge || (age == bestAge && b - a < bestB - bestA))
            bestA = a, bestB = b, bestAge = age;
    }
    if (bestA < 0)
        return -1;
    for (k = bestA; k < bestB; k++) s->resOf[s->res[k].set] = 0;
    for (k = bestB; k < s->nres; k++) {
        s->res[k - (bestB - bestA)]               = s->res[k];
        s->resOf[s->res[k - (bestB - bestA)].set] = (uint8_t)(k - (bestB - bestA) + 1);
    }
    s->nres -= bestB - bestA;
    s->evicted += bestB - bestA;
    return res_insert(s, bestA, bestA ? s->res[bestA - 1].base + s->res[bestA - 1].n : s->cellLo, set, n);
}

static void put_cells(void *out, uint16_t addr, const uint8_t *c, int words)
{
#if defined(__sh__)
    sgb_write_run(out, addr, (const uint16_t *)c, words); /* big-endian like the pack */
#else
    uint16_t w[16];
    int i, k;
    for (i = 0; i < words; i += 16) {
        int n = words - i < 16 ? words - i : 16;
        for (k = 0; k < n; k++) w[k] = mds_rd16(c + 2 * (i + k));
        sgb_write_run(out, (uint16_t)(addr + 2 * i), w, n);
    }
#endif
}

void mds_record(MdSprites *s, void *out)
{
    SgbOut *o        = (SgbOut *)out;
    const uint8_t *p = s->pack;
    int i, budget = MDS_UPLOAD_MAX, hw = 0;
    uint16_t sat[4 * MDS_MAX_HW];
    if (!p)
        return;
    s->frame++;
    const uint8_t *sets = p + RD32(p + 20), *pieces = p + RD32(p + 24);
    /* 1. every set drawn this frame in VRAM: first the ones already there are marked (so none of them is evicted),
       then the others are uploaded - as many as the budget takes (a set bigger than the whole budget goes alone) */
    for (i = 0; i < s->ndraw; i++) {
        int r = s->resOf[s->draw[i].set];
        if (r)
            s->res[r - 1].used = s->frame;
    }
    for (i = 0; i < s->ndraw; i++) {
        MdsDraw *d = &s->draw[i];
        int r      = s->resOf[d->set];
        MdsRes *e;
        if (!r) {
            const uint8_t *st = sets + S_SIZE * d->set;
            if (budget <= 0 || (r = res_alloc(s, d->set, RD16(st + 4)) + 1) <= 0) {
                d->set = 0xFFFF; /* next frame */
                s->dropped++;
                continue;
            }
            e             = &s->res[r - 1];
            e->firstPiece = RD16(st + 6);
            e->np         = st[8];
            e->line       = st[9];
            e->cw         = st[10];
            e->ch         = st[11];
            e->done       = 0;
        }
        e = &s->res[r - 1];
        if (e->done < e->n) { /* (the rest of) its cells, as far as the budget goes */
            int k = e->n - e->done;
            if (k * 16 > budget)
                k = budget / 16;
            if (k > 0) {
                const uint8_t *st = sets + S_SIZE * d->set;
                put_cells(o, (uint16_t)((e->base + e->done) * 32), p + RD32(st) + 32 * e->done, k * 16);
                e->done = (uint16_t)(e->done + k);
                budget -= k * 16;
                s->uploadWords += k * 16;
            }
            if (e->done < e->n) {
                d->set = 0xFFFF; /* drawn once all of it is in VRAM */
                s->dropped++;
            }
        }
    }
    /* 2. the sprite table, the last draw (in front) first; one write group */
    for (i = s->ndraw - 1; i >= 0 && hw < MDS_MAX_HW; i--) {
        MdsDraw *d = &s->draw[i];
        if (d->set == 0xFFFF)
            continue;
        const MdsRes *e = &s->res[s->resOf[d->set] - 1];
        int np = e->np, cw = e->cw, ch = e->ch, k;
        int attr = ((d->flags & MDS_HIGH) ? 0x8000 : 0) | e->line << 13 | ((d->flags & MDS_VFLIP) ? 0x1000 : 0) |
                   ((d->flags & MDS_HFLIP) ? 0x0800 : 0);
        const uint8_t *pc = pieces + P_SIZE * e->firstPiece;
        for (k = 0; k < np && hw < MDS_MAX_HW; k++, pc += P_SIZE) {
            int size = pc[2], pw = (size >> 2 & 3) + 1, ph = (size & 3) + 1;
            int x = d->x + 8 * ((d->flags & MDS_HFLIP) ? cw - pc[0] - pw : pc[0]);
            int y = d->y + 8 * ((d->flags & MDS_VFLIP) ? ch - pc[1] - ph : pc[1]);
            uint16_t *t = &sat[4 * hw];
            if (x >= 320 || y >= 224 || x + pw * 8 <= 0 || y + ph * 8 <= 0)
                continue;
            t[0] = (uint16_t)((y + 128) & 0x1FF);
            t[1] = (uint16_t)(size << 8 | (hw + 1));
            t[2] = (uint16_t)(attr | ((e->base + pc[3]) & 0x7FF));
            t[3] = (uint16_t)((x + 128) & 0x1FF);
            hw++;
        }
    }
    if (!hw) { /* no sprite: entry 0 above the screen, the chain ends there */
        sat[0] = sat[1] = sat[2] = sat[3] = 0;
        hw = 1;
    }
    sat[4 * hw - 3] &= 0xFF00; /* the last entry links to 0 */
    sgb_write_run(o, MDS_SAT, sat, 4 * hw);
    if (hw > s->hwMax)
        s->hwMax = hw;
    s->ndraw = 0;
}
