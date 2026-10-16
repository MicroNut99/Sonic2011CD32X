/* PORT32X: see sh2_frames.h */
#include "sh2_frames.h"

#define RD16(p) ((uint16_t)(((const uint8_t *)(p))[0] << 8 | ((const uint8_t *)(p))[1]))
#define RD32(p) ((uint32_t)RD16(p) << 16 | RD16((const uint8_t *)(p) + 2))

static const uint8_t *decode_runs(const uint8_t *p, int lines, int *tile, int *x)
{
    int n, k, y = 0;
    *tile = RD16(p);
    n     = RD16(p + 2);
    p += 4;
    for (k = 0; k < n; k++) {
        int cnt = RD16(p), v = RD16(p + 2);
        p += 4;
        while (cnt-- > 0 && y < lines) x[y++] = v;
    }
    while (y < lines) x[y++] = 0;
    return p;
}

static void layer(Sh2Frames *s, MdLayer *L, int at)
{
    L->chunks       = s->words + RD32(s->bytes + 24) / 2;
    L->quads        = s->words + RD32(s->bytes + 20) / 2;
    L->layout       = s->words + RD32(s->bytes + at) / 2;
    L->layoutStride = L->wChunks = RD16(s->bytes + at + 4);
    L->hChunks      = RD16(s->bytes + at + 6);
    L->showBack     = s->bytes[at + 8];
    L->showFront    = s->bytes[at + 9];
}

int sh2f_init(Sh2Frames *s, const uint8_t *bytes, const uint16_t *words, uint16_t *wAddr, uint16_t *wWord)
{
    int i;
    if (RD32(bytes) != 0x53434D44u || RD16(bytes + 4) != 3)
        return -1;
    s->bytes   = bytes;
    s->words   = words;
    s->lines   = RD16(bytes + 6);
    s->nframes = RD16(bytes + 10);
    if (s->lines > SH2F_LINES_MAX)
        s->lines = SH2F_LINES_MAX;
    s->frames = s->fp = bytes + RD32(bytes + 32);
    s->frame  = 0;
    layer(s, &s->LA, 36);
    s->hasB = RD16(bytes + 52) != 0;
    if (s->hasB)
        layer(s, &s->LB, 48);
    for (i = 0; i < 64; i++) s->cram0[i] = s->cramShown[i] = RD16(bytes + RD32(bytes + 28) + 2 * i);
    s->w.addr = wAddr;
    s->w.word = wWord;
    s->w.n    = 0;
    s->w.max  = SH2F_WRITES_MAX;
    return 0;
}

int sh2f_next(Sh2Frames *s, uint16_t *out, int outMax)
{
    uint16_t cIdx[64], cCol[64];
    int tA, tB, n, i, nc = 0;
    const uint8_t *p = s->fp;
    if (s->frame == 0) { /* start or loop: planes from scratch, palette from the file's start */
        md_plane_reset(&s->pA);
        md_plane_reset(&s->pB);
        for (i = 0; i < 64; i++) s->cramNow[i] = s->cram0[i];
    }
    p = decode_runs(p, s->lines, &tA, s->xA);
    p = decode_runs(p, s->lines, &tB, s->xB);
    n = RD16(p);
    p += 2;
    for (i = 0; i < n; i++, p += 4) s->cramNow[RD16(p) & 63] = RD16(p + 2);
    for (i = 0; i < 64; i++)
        if (s->cramNow[i] != s->cramShown[i]) {
            cIdx[nc]       = (uint16_t)i;
            cCol[nc++]     = s->cramNow[i];
            s->cramShown[i] = s->cramNow[i];
        }
    n = md_record_frame(&s->pA, &s->LA, tA, s->xA, s->hasB ? &s->pB : 0, s->hasB ? &s->LB : 0, tB, s->xB, s->lines, 320, cIdx, cCol, nc, &s->w,
                        s->hsA, s->hsB, out, outMax);
    s->fp = p;
    if (++s->frame >= s->nframes) {
        s->frame = 0;
        s->fp    = s->frames;
    }
    return n;
}
