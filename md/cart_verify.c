/* PORT32X S6b: see cart_verify.h */
#include "cart_verify.h"

static uint32_t rd32(CartRead16 rd, uint32_t at) { return (uint32_t)rd(at) << 16 | rd(at + 2); }

int cart_verify(CartRead16 rd, CartCheck *r)
{
    uint32_t k;
    r->version = r->parts = r->bytes = 0;
    r->badPart = -1;
    r->okParts = 0;
    if (rd(0) != 0x5343 || rd(2) != 0x4342) /* "SCCB" */
        return 1;
    r->version = rd32(rd, 4);
    r->parts   = rd32(rd, 8);
    if (r->version != 2 || r->parts == 0 || r->parts > 64)
        return 1;
    for (k = 0; k < r->parts; k++) {
        uint32_t e = 12 + 20 * k, off = rd32(rd, e), n = rd32(rd, e + 4), want = rd32(rd, e + 16), i;
        uint32_t c = 0xFFFFFFFFu;
        if (off & 1)
            return 1;
        for (i = 0; i < n; i += 2) { /* 16 bits at a time; the last byte of an odd length alone */
            uint16_t w = rd(off + i);
            int b, m = (i + 1 < n) ? 2 : 1;
            for (b = 0; b < m; b++) {
                int bit;
                c ^= b ? (w & 0xFF) : (w >> 8);
                for (bit = 0; bit < 8; bit++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
            }
        }
        if (~c != want) {
            if (r->badPart < 0)
                r->badPart = (int)k;
        }
        else
            r->okParts++;
        if (off + n > r->bytes)
            r->bytes = off + n;
    }
    return r->badPart >= 0 ? 2 : 0;
}
