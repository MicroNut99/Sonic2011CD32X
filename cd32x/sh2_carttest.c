/* PORT32X S6b: the cart test (SH2). One stage's console cart image goes from the CD into the RAM
 * cart through Kobo's proven path, and the SH2 then checks it where it landed:
 *   1. Kobo's start-up handshake (COMM10 0x0AAA / 0x0BBB)
 *   2. Kobo's load_pack(): CMD 38 opens file 16 (the Sub-CPU maps it to "KOBOGFX.BIN" - here the
 *      stage image), CMD 46 per 32 KB chunk copies it into the cart at 2 MB; the Sub-CPU prints its
 *      own "CART I16 C.. OK" read-back line per chunk in green and reports errors in COMM12
 *   3. md/cart_verify.c reads the image back from the cart (0x22200000, 16-bit reads only - Kobo)
 *      and checks the header and every part's CRC32
 *   4. the result on the 32X layer: a small black box in the bottom-right corner, two lines, Kobo's
 *      3x5 font; the rest of the 32X layer is see-through, so Chilly's WHITE start-up lines and Kobo's
 *      GREEN diagnostics stay readable (S6b2: the S6b box covered the white lines)
 * Kobo's take_fb / flip_wait / comm_pause / wait_cmd / load_pack / font are copied as they are, except
 * that text is drawn with 16-bit frame-buffer writes (Kobo rule 4). */
#include <stddef.h>
#include <stdint.h>
#include "32x.h"
#include "../md/cart_verify.h"

const char sh2_build_marker[] = "SH2-VS6B"; /* the Sub-CPU looks for "SH2-V" */

#define SC_COMM2 (*(volatile uint16_t *)0x20004022)
#define SC_COMM8_32 (*(volatile uint32_t *)0x20004028)
#define SC_COMM10 (*(volatile uint16_t *)0x2000402A)
#define SC_COMM12 (*(volatile uint16_t *)0x2000402C)
#define SC_CMD_CLR (*(volatile uint16_t *)0x2000401A)
#define SC_SYS_ADAPTER_B (*(volatile uint8_t *)0x20004000)
#define SC_VDP_FBCTL (*(volatile uint16_t *)0x2000410A)
#define SC_CRAM ((volatile uint16_t *)0x20004200)
#define SC_FRAMEBUFFER ((volatile uint16_t *)0x24000000)

#define CMD_OPEN_FILE 38      /* Kobo */
#define CMD_CHUNK_TO_CART 46  /* Kobo */
#define FILE_KOBOGFX 16       /* Kobo: main.c maps file number 16 to "KOBOGFX.BIN" */
#define PACK_MAX 0x100000     /* Kobo */
#define CHUNK_BYTES 32768     /* Kobo */
#define CART_IMAGE 0x22200000 /* Kobo CART_RAM_SH2 0x22000000 + KOBO_PACK_SLOT 16 x CART_SLOT 0x20000 */

#define SCR_W 320
#define SCR_H 224
#define BOX_X 208 /* S6b2: the white lines end at about x 200 */
#define BOX_Y 208
#define BOX_W (SCR_W - BOX_X)

void pri_vbi_handler(void) {}
void pri_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_dma1_handler(void) {}
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;

void *memset(void *d, int c, size_t n) /* Kobo K21b */
{
    volatile uint8_t *p = (volatile uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    volatile uint8_t *p       = (volatile uint8_t *)d;
    const volatile uint8_t *q = (const volatile uint8_t *)s;
    while (n--) *p++ = *q++;
    return d;
}
void secondary(void)
{
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    while (1) {
    }
}

static void take_fb(void) /* Kobo */
{
    SC_SYS_ADAPTER_B = 0x80;
    while ((MARS_SYS_INTMSK & 0x8000) == 0) {
    }
}
static void flip_wait(void) /* Kobo */
{
    uint16_t cur = SC_VDP_FBCTL & 1;
    SC_VDP_FBCTL = cur ^ 1;
    while ((SC_VDP_FBCTL & 1) == cur) {
    }
}
static void comm_pause(void) /* Kobo HW6 */
{
    volatile int k;
    for (k = 0; k < 64; k++) {
    }
}
static void wait_cmd(uint16_t cmd) /* Kobo (without its vblank counter) */
{
    uint32_t n;
    MARS_SYS_COMM0 = cmd;
    for (n = 0; n < 800000UL && MARS_SYS_COMM0 != 0; n++) comm_pause();
}
static uint32_t pack_len;
static int load_pack(void) /* Kobo (cart mode): bad chunk count, -1 = not found */
{
    uint32_t len, nchunks, c;
    int bad = 0;
    SC_COMM2 = FILE_KOBOGFX;
    wait_cmd(CMD_OPEN_FILE);
    len = SC_COMM8_32;
    if ((int32_t)len <= 0)
        return -1;
    if (len > PACK_MAX)
        len = PACK_MAX;
    pack_len    = len;
    SC_COMM8_32 = len;
    nchunks     = (len + CHUNK_BYTES - 1) / CHUNK_BYTES;
    for (c = 0; c < nchunks; c++) {
        SC_COMM2                           = (uint16_t)c;
        *(volatile uint16_t *)0x2000402C = 0; /* HW2: 16-bit - COMM14 is the Sub heartbeat */
        wait_cmd(CMD_CHUNK_TO_CART);
        if (*(volatile uint16_t *)0x2000402C != 0)
            bad++;
    }
    return bad;
}

/* ---- text: Kobo's 3x5 font, drawn with 16-bit writes ---- */
static const uint8_t font3x5[][5] = { /* Kobo */
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 }, /* 0 1 2 3 4 */
    { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 }, /* 5 6 7 8 9 */
    { 2, 5, 7, 5, 5 }, { 6, 5, 6, 5, 6 }, { 3, 4, 4, 4, 3 }, { 6, 5, 5, 5, 6 }, { 7, 4, 7, 4, 7 }, /* A B C D E */
    { 7, 4, 7, 4, 4 }, { 5, 5, 6, 5, 5 }, { 4, 4, 4, 4, 7 }, { 5, 7, 7, 5, 5 }, { 6, 5, 6, 5, 5 }, /* F K L N R */
    { 5, 5, 5, 5, 2 }, { 0, 0, 0, 0, 0 }, { 7, 2, 2, 2, 2 }, { 5, 5, 2, 5, 5 }, { 5, 5, 2, 2, 2 }, /* V space T X Y */
    { 7, 4, 5, 5, 7 }, { 5, 5, 7, 5, 5 }, { 7, 2, 2, 2, 7 }, { 5, 7, 7, 5, 5 }, { 7, 5, 5, 5, 7 }, /* G H I M O */
    { 7, 5, 7, 4, 4 }, { 5, 5, 5, 5, 7 }, { 5, 5, 7, 7, 5 }                                         /* P U W */
};
static int glyph(char c) /* Kobo */
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    switch (c) {
        case 'K': return 16;
        case 'L': return 17;
        case 'N': return 18;
        case 'R': return 19;
        case 'S': return 5;
        case 'V': return 20;
        case 'T': return 22;
        case 'X': return 23;
        case 'Y': return 24;
        case 'G': return 25;
        case 'H': return 26;
        case 'I': return 27;
        case 'M': return 28;
        case 'O': return 29;
        case 'P': return 30;
        case 'U': return 31;
        case 'W': return 32;
    }
    return 21;
}
enum { PX_CLEAR = 0, PX_BOX = 1, PX_GREEN = 2, PX_RED = 3, PX_WHITE = 4 };
static void put_text(int x, int y, const char *t, uint8_t c) /* x a multiple of 4 */
{
    volatile uint16_t *fb = SC_FRAMEBUFFER;
    for (; *t; t++, x += 4) {
        const uint8_t *g = font3x5[glyph(*t)];
        int r;
        for (r = 0; r < 5; r++) {
            uint8_t p0 = (g[r] & 4) ? c : PX_BOX, p1 = (g[r] & 2) ? c : PX_BOX, p2 = (g[r] & 1) ? c : PX_BOX;
            uint32_t w = 0x100 + ((y + r) * SCR_W + x) / 2;
            fb[w]      = (uint16_t)(p0 << 8 | p1);
            fb[w + 1]  = (uint16_t)(p2 << 8 | PX_BOX);
        }
    }
}
static char *hex(char *t, uint32_t v, int digits) /* Kobo */
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        int n = v & 15;
        t[i]  = (char)(n < 10 ? '0' + n : 'A' + n - 10);
        v >>= 4;
    }
    t[digits] = 0;
    return t + digits;
}
static char *str(char *t, const char *s)
{
    while (*s) *t++ = *s++;
    *t = 0;
    return t;
}

static void frame(void) /* line table, everything transparent, the black box */
{
    volatile uint16_t *fb = SC_FRAMEBUFFER;
    int i;
    for (i = 0; i < SCR_H; i++) fb[i] = (uint16_t)(0x100 + i * (SCR_W / 2));
    for (i = 0; i < SCR_W * SCR_H / 2; i++) {
        int y = i / (SCR_W / 2), x = (i % (SCR_W / 2)) * 2;
        fb[0x100 + i] = (y >= BOX_Y && x >= BOX_X) ? (PX_BOX << 8 | PX_BOX) : 0;
    }
}
static void show(int row, const char *t, uint8_t c) /* row 0 or 1 of the box, into both frame buffers */
{
    int k;
    for (k = 0; k < 2; k++) {
        put_text(BOX_X + 4, BOX_Y + 2 + row * 7, "                           ", PX_BOX); /* 27 x 4 = 108 pixels */
        put_text(BOX_X + 4, BOX_Y + 2 + row * 7, t, c);
        flip_wait();
    }
}

static uint16_t cart_read16(uint32_t offset) { return *(volatile uint16_t *)(CART_IMAGE + offset); }

int main(void)
{
    char t[48], *p;
    CartCheck r;
    int bad, res;

    SC_COMM10      = 0x0004;
    MARS_SYS_COMM0 = 0;
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    SC_COMM10 = 0x0AAA; /* "SH2 ready" (Kobo) */
    while (SC_COMM10 != 0x0BBB) {
    }

    take_fb();
    MARS_VDP_DISPMODE = MARS_VDP_MODE_OFF;
    SC_CRAM[PX_CLEAR] = 0x0000; /* see-through: Kobo's Genesis text shows */
    SC_CRAM[PX_BOX]   = 0x0400; /* black (Kobo: never 0x0000) */
    SC_CRAM[PX_GREEN] = 0x03E0;
    SC_CRAM[PX_RED]   = 0x001F;
    SC_CRAM[PX_WHITE] = 0x7FFF;
    frame();
    flip_wait();
    frame();
    MARS_VDP_DISPMODE = (MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256; /* as Kobo: keep bit 15 */
    show(0, "S6B CART TEST", PX_WHITE);
    show(1, "LOADING IMAGE 16", PX_WHITE);

    bad = load_pack();
    if (bad < 0) {
        show(1, "IMAGE NOT FOUND ON CD", PX_RED);
        while (1) {
        }
    }
    p = str(t, "IMG ");
    p = hex(p, pack_len, 6);
    p = str(p, " BADCHUNK ");
    hex(p, (uint32_t)bad, 2);
    show(0, t, bad ? PX_RED : PX_GREEN);
    show(1, "CHECKING THE CART", PX_WHITE);

    res = cart_verify(cart_read16, &r);
    if (res == 1)
        str(t, "NOT AN IMAGE  HEADER BAD");
    else {
        p = str(t, res == 0 ? "OK  PARTS " : "BAD PARTS ");
        p = hex(p, (uint32_t)r.okParts, 2);
        p = str(p, " OF ");
        p = hex(p, r.parts, 2);
        if (r.badPart >= 0) {
            p = str(p, " FIRST ");
            hex(p, (uint32_t)r.badPart, 2);
        }
    }
    show(1, t, res == 0 ? PX_GREEN : PX_RED);
    while (1) {
    }
}
