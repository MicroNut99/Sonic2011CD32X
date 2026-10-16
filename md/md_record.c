/* PORT32X: see md_record.h */
#include "md_record.h"

void md_writes_collect(void *ctx, uint16_t addr, uint16_t word)
{
    MdWrites *w = (MdWrites *)ctx;
    if (w->n < w->max) {
        w->addr[w->n] = addr;
        w->word[w->n] = word;
        w->n++;
    }
}

static int put_runs(uint16_t *out, int pos, int outMax, const int16_t *hs, int lines)
{
    int at = pos++, n = 0, y = 0;
    while (y < lines) {
        int k = y;
        while (k < lines && hs[k] == hs[y]) k++;
        if (pos + 2 > outMax)
            return -1;
        out[pos++] = (uint16_t)(k - y);
        out[pos++] = (uint16_t)hs[y];
        n++;
        y = k;
    }
    out[at] = (uint16_t)n;
    return pos;
}

int md_record_frame(MdPlane *pA, const MdLayer *LA, int tA, const int *xA, MdPlane *pB, const MdLayer *LB, int tB, const int *xB,
                    int lines, int screenW, const uint16_t *cramIdx, const uint16_t *cramCol, int ncram, MdWrites *w,
                    int16_t *hsA, int16_t *hsB, uint16_t *out, int outMax)
{
    uint16_t vA = 0, vB = 0;
    int pos, i, gat, groups = 0;
    w->n = 0;
    md_plane_update(pA, LA, 0xC000, tA, xA, lines, screenW, hsA, &vA, md_writes_collect, w);
    if (pB && LB)
        md_plane_update(pB, LB, 0xE000, tB, xB, lines, screenW, hsB, &vB, md_writes_collect, w);
    else
        for (i = 0; i < lines; i++) hsB[i] = 0;
    if (outMax < 8)
        return -1;
    out[0] = vA;
    out[1] = vB;
    pos    = put_runs(out, 2, outMax, hsA, lines);
    if (pos < 0 || (pos = put_runs(out, pos, outMax, hsB, lines)) < 0)
        return -1;
    gat = pos++;
    for (i = 0; i < w->n;) {
        int k = i + 1, j;
        while (k < w->n && w->addr[k] == (uint16_t)(w->addr[k - 1] + 2)) k++;
        if (pos + 2 + (k - i) > outMax)
            return -1;
        out[pos++] = w->addr[i];
        out[pos++] = (uint16_t)(k - i);
        for (j = i; j < k; j++) out[pos++] = w->word[j];
        groups++;
        i = k;
    }
    out[gat] = (uint16_t)groups;
    if (pos + 1 + 2 * ncram > outMax)
        return -1;
    out[pos++] = (uint16_t)ncram;
    for (i = 0; i < ncram; i++) {
        out[pos++] = cramIdx[i];
        out[pos++] = cramCol[i];
    }
    return pos;
}
