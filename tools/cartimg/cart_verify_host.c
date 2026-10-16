/* PORT32X S6b: md/cart_verify.c on the PC - the same check the SH2 runs, on an image file */
#include "../../md/cart_verify.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned char *img;
static long imgLen;
static uint16_t rd(uint32_t at) { return (uint16_t)((at < (uint32_t)imgLen ? img[at] : 0) << 8 | (at + 1 < (uint32_t)imgLen ? img[at + 1] : 0)); }
int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    CartCheck r;
    int res;
    if (!f)
        return fprintf(stderr, "usage: cart_verify_host <image.bin>\n"), 2;
    fseek(f, 0, SEEK_END);
    imgLen = ftell(f);
    fseek(f, 0, SEEK_SET);
    img = malloc(imgLen);
    if (fread(img, 1, imgLen, f) != (size_t)imgLen)
        return 2;
    fclose(f);
    res = cart_verify(rd, &r);
    printf("cart image check: %s (version %u, %u parts, %u OK, first bad part %d, %u bytes)\n",
           res == 0 ? "OK" : res == 1 ? "NOT AN IMAGE" : "BAD", r.version, r.parts, r.okParts, r.badPart, r.bytes);
    return res;
}
