/* PORT32X S6b: check a console cart image ("SCCB" v2) where it lies - on the SH2 in the cart, on the
 * PC in a file. Reads only through read16(byte offset) (the cart is read 16 bits at a time, Kobo
 * rule), checks the header and each part's CRC32. Portable C, no library. */
#ifndef CART_VERIFY_H
#define CART_VERIFY_H
#include <stdint.h>
typedef uint16_t (*CartRead16)(uint32_t offset); /* offset even; big-endian word */
typedef struct {
    uint32_t version, parts, bytes; /* bytes: end of the last part */
    int badPart;                    /* first part whose CRC differs, -1 = none */
    int okParts;
} CartCheck;
/* 0 = all good, 1 = not an image / bad header, 2 = a part's CRC differs */
int cart_verify(CartRead16 rd, CartCheck *r);
#endif
