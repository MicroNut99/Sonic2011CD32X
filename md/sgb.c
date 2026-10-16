/* REV R1: see sgb.h */
#include "sgb.h"

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
