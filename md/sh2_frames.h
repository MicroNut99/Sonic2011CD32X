/* PORT32X: S4 - what the SH2 computes each frame: from a format-3 MDPLANE data file (stage tables
 * + per-frame camera runs, mdplane_data.h), stream both planes with md_planes.c and build the
 * frame's record (md_record.c) for the 68000. Portable C: the PC test runs exactly this code. */
#ifndef SH2_FRAMES_H
#define SH2_FRAMES_H
#include <stdint.h>
#include "md_planes.h"
#include "md_record.h"

#define SH2F_LINES_MAX 240
#define SH2F_WRITES_MAX 4096

typedef struct {
    const uint8_t *bytes;  /* the data file */
    const uint16_t *words; /* the same file as native 16-bit words (on the SH2: the same memory) */
    const uint8_t *frames, *fp;
    int lines, nframes, frame, hasB;
    MdLayer LA, LB;
    MdPlane pA, pB;
    uint16_t cram0[64], cramNow[64], cramShown[64];
    int xA[SH2F_LINES_MAX], xB[SH2F_LINES_MAX];
    int16_t hsA[SH2F_LINES_MAX], hsB[SH2F_LINES_MAX];
    MdWrites w;
} Sh2Frames;

/* 0 = ok, -1 = not a format-3 file */
int sh2f_init(Sh2Frames *s, const uint8_t *bytes, const uint16_t *words, uint16_t *wAddr, uint16_t *wWord);
/* the next frame's record into out (16-bit words); returns its length in words, -1 on overflow */
int sh2f_next(Sh2Frames *s, uint16_t *out, int outMax);
#endif
