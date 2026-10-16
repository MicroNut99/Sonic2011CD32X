/* PORT32X: S4 - one frame's VDP work as a record (format 2 in mdplane_data.h):
 *   V scroll A, V scroll B, H scroll A runs, H scroll B runs, name-table write groups, palette changes.
 * Built by the PC exporter and, from S4, live by the SH2 - the same code, so both agree.
 * Output: 16-bit words in native order (the caller stores them big-endian). No host deps. */
#ifndef MD_RECORD_H
#define MD_RECORD_H
#include <stdint.h>
#include "md_planes.h"

typedef struct {
    uint16_t *addr, *word; /* scratch for the name-table writes */
    int n, max;
} MdWrites;

void md_writes_collect(void *ctx, uint16_t addr, uint16_t word); /* MdWriteFn, ctx = MdWrites* */

/* Streams both planes (pB/LB may be 0: no background), then writes the record into out.
 * cramIdx/cramCol: n palette changes. Returns the record length in words (or -1: out too small). */
int md_record_frame(MdPlane *pA, const MdLayer *LA, int tA, const int *xA, MdPlane *pB, const MdLayer *LB, int tB, const int *xB,
                    int lines, int screenW, const uint16_t *cramIdx, const uint16_t *cramCol, int ncram, MdWrites *w,
                    int16_t *hsA, int16_t *hsB, uint16_t *out, int outMax);
#endif
