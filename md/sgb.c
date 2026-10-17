/* REV R1: see sgb.h */
#include "sgb.h"
/* PORT32X R1V: code the SH2 runs every frame - in the engine build it goes into SDRAM (cd32x/engine/engine_cart.ld
   .sdtext; a code cache miss costs 11 cycles there, 87 in the cart). Not in the other builds (MARS: Kobo's SH2
   programs; the PC). */
#if defined(__sh__) && !defined(MARS)
#define SCD_MD_HOT __attribute__((section(".sdtext")))
#else
#define SCD_MD_HOT
#endif
void sgb_out_begin(SgbOut *o, uint16_t *out, int max) SCD_MD_HOT;
uint16_t sgb_md_color(uint16_t c) SCD_MD_HOT;
void sgb_write(void *ctx, uint16_t addr, uint16_t word) SCD_MD_HOT;
void sgb_write_run(void *ctx, uint16_t addr, const uint16_t *words, int n) SCD_MD_HOT;
int sgb_out_end(SgbOut *o, uint16_t vA, uint16_t vB, const int16_t *hsA, const int16_t *hsB, int lines,
                const uint16_t *cramIdx, const uint16_t *cramCol, int ncram) SCD_MD_HOT;

uint16_t sgb_fade565(uint16_t c, int r, int g, int b, int a)
{
    int r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31, k;
    if (a <= 0)
        return c;
    if (a > 255)
        a = 255;
    k  = 255 - a;
    r5 = (r5 * k + (r >> 3) * a) / 255;
    g6 = (g6 * k + (g >> 2) * a) / 255;
    b5 = (b5 * k + (b >> 3) * a) / 255;
    return (uint16_t)(r5 << 11 | g6 << 5 | b5);
}

void sgb_out_begin(SgbOut *o, uint16_t *out, int max)
{
    o->out      = out;
    o->max      = max;
    o->pos      = 2; /* [0] length, [1] groups - filled in by sgb_out_end */
    o->groups   = 0;
    o->cntAt    = 0;
    o->cnt      = 0;
    o->last     = 0;
    o->overflow = 0;
}

void sgb_write(void *ctx, uint16_t addr, uint16_t word)
{
    SgbOut *o = (SgbOut *)ctx;
    if (o->cntAt && addr == (uint16_t)(o->last + 2)) {
        if (o->pos + 1 > o->max) {
            o->overflow = 1;
            return;
        }
        o->out[o->pos++] = word;
        o->cnt++;
    }
    else {
        if (o->pos + 3 > o->max) {
            o->overflow = 1;
            return;
        }
        if (o->cntAt)
            o->out[o->cntAt] = (uint16_t)o->cnt; /* close the group before */
        o->out[o->pos++] = addr;
        o->cntAt         = o->pos++;
        o->out[o->pos++] = word;
        o->cnt           = 1;
        o->groups++;
    }
    o->last = addr;
}

void sgb_write_run(void *ctx, uint16_t addr, const uint16_t *words, int n)
{
    SgbOut *o = (SgbOut *)ctx;
    int i = 0;
    if (n <= 0)
        return;
    if (o->overflow || !o->cntAt || addr != (uint16_t)(o->last + 2)) {
        sgb_write(ctx, addr, words[0]); /* the first word opens a group (or does what sgb_write does) */
        i = 1;
    }
    if (o->overflow || o->pos + (n - i) > o->max) { /* no room for the rest: word by word, as the calls would */
        for (; i < n; i++) sgb_write(ctx, (uint16_t)(addr + 2 * i), words[i]);
        return;
    }
    o->cnt += n - i; /* the rest extend the open group */
    for (; i < n; i++) o->out[o->pos++] = words[i];
    o->last = (uint16_t)(addr + 2 * (n - 1));
}

static int put_runs(SgbOut *o, const int16_t *hs, int lines) SCD_MD_HOT;
static int put_runs(SgbOut *o, const int16_t *hs, int lines)
{
    int at = o->pos++, n = 0, y = 0;
    while (y < lines) {
        int k = y;
        while (k < lines && hs[k] == hs[y]) k++;
        if (o->pos + 2 > o->max)
            return -1;
        o->out[o->pos++] = (uint16_t)(k - y);
        o->out[o->pos++] = (uint16_t)hs[y];
        n++;
        y = k;
    }
    o->out[at] = (uint16_t)n;
    return 0;
}

int sgb_out_end(SgbOut *o, uint16_t vA, uint16_t vB, const int16_t *hsA, const int16_t *hsB, int lines,
                const uint16_t *cramIdx, const uint16_t *cramCol, int ncram)
{
    int i;
    if (o->overflow)
        return -1;
    if (o->cntAt)
        o->out[o->cntAt] = (uint16_t)o->cnt;
    o->out[1] = (uint16_t)o->groups;
    if (o->pos + 2 + 2 > o->max)
        return -1;
    o->out[o->pos++] = vA;
    o->out[o->pos++] = vB;
    if (put_runs(o, hsA, lines) < 0 || put_runs(o, hsB, lines) < 0)
        return -1;
    if (o->pos + 1 + 2 * ncram > o->max)
        return -1;
    o->out[o->pos++] = (uint16_t)ncram;
    for (i = 0; i < ncram; i++) {
        o->out[o->pos++] = cramIdx[i];
        o->out[o->pos++] = cramCol[i];
    }
    o->out[0] = (uint16_t)(o->pos - 1);
    return o->pos;
}

uint16_t sgb_md_color(uint16_t c)
{
    /* RGB565 -> 3 bits a channel (as md_check.cpp rgb9_of565), then 0000 BBB0 GGG0 RRR0 */
    unsigned r = (c >> 13) & 7, g = (c >> 8) & 7, b = (c >> 2) & 7;
    return (uint16_t)(b << 9 | g << 5 | r << 1);
}

int sgb_apply(const uint16_t *rec, int lines, SgbVramFn vram_w, void *ctx, int16_t *hsA, int16_t *hsB, uint16_t *vA,
              uint16_t *vB, uint16_t *cram)
{
    const uint16_t *p = rec + 1;
    int g, i, k, pass;
    int groups = *p++;
    for (g = 0; g < groups; g++) {
        uint16_t addr = *p++;
        int n         = *p++;
        for (k = 0; k < n; k++, addr = (uint16_t)(addr + 2)) vram_w(ctx, addr, *p++);
    }
    *vA = *p++;
    *vB = *p++;
    for (pass = 0; pass < 2; pass++) {
        int16_t *hs = pass ? hsB : hsA;
        int runs = *p++, y = 0;
        for (i = 0; i < runs; i++) {
            int n     = *p++;
            int16_t v = (int16_t)*p++;
            for (k = 0; k < n && y < lines; k++) hs[y++] = v;
        }
    }
    k = *p++;
    for (i = 0; i < k; i++) {
        uint16_t idx = *p++;
        uint16_t col = *p++;
        if (idx < 64)
            cram[idx] = col;
    }
    return (int)(p - rec);
}
