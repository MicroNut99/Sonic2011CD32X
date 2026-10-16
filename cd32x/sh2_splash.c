/* PORT32X: the master SH2 during loading: the splash screen with a red progress bar on the
 * 32X layer (256-colour mode, 320x224), over the Mega Drive picture.
 *   - start handshake (COMM10 0x0AAA / 0x0BBB, as Kobo)
 *   - splash (splash/SPLASH.SPL, tools/make_splash.py) into both frame buffers, 32X layer on
 *   - COMM12 = progress bar width in pixels (0..BAR_IN), written by the Sub-CPU through the 68K
 *   - COMM2 = 0x5344 ("SD"): the Genesis picture is ready (or an error must be seen): 32X off
 * One writer per COMM register, pauses between reads (Kobo hardware rule 2 / HW6); frame
 * buffer written with 16-bit writes only (Kobo rule 4); colour 0x0000 never used (rule 5).
 * Kobo's take_fb / flip_wait / line table are used as they are. */
#include <stdint.h>
#include "32x.h"

const char sh2_build_marker[] = "SH2-VS3C"; /* the Sub-CPU looks for "SH2-V" */

#define SC_COMM2 (*(volatile uint16_t *)0x20004022)
#define SC_COMM10 (*(volatile uint16_t *)0x2000402A)
#define SC_COMM12 (*(volatile uint16_t *)0x2000402C)
#define SC_CMD_CLR (*(volatile uint16_t *)0x2000401A)
#define SC_SYS_ADAPTER_B (*(volatile uint8_t *)0x20004000)
#define SC_VDP_FBCTL (*(volatile uint16_t *)0x2000410A)
#define SC_CRAM ((volatile uint16_t *)0x20004200)
#define SC_FRAMEBUFFER ((volatile uint16_t *)0x24000000)

#define SCR_W 320
#define SCR_H 224
/* the progress bar (keep in step with tools/make_splash.py): frame colour 254, red 255 */
#define BAR_X 40
#define BAR_Y 204
#define BAR_W 240
#define BAR_H 8
#define BAR_IN (BAR_W - 2)
#define COL_FRAME 254
#define COL_RED 255
#define GO_SIGNAL 0x5344

extern const uint8_t splash[]; /* splash.s: 512 bytes palette + 320x224 indices */

void pri_vbi_handler(void) {}
void pri_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_dma1_handler(void) {}
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;

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
    while ((SC_VDP_FBCTL & 1) == cur)
        ;
}

static void comm_pause(void) /* Kobo HW6: ~20 us between COMM reads */
{
    volatile int k;
    for (k = 0; k < 64; k++) {
    }
}

/* the bar into the frame buffer being drawn: frame colour around, red up to `fill` */
static void draw_bar(int fill)
{
    volatile uint16_t *fb = SC_FRAMEBUFFER;
    int x, y;
    for (y = BAR_Y; y < BAR_Y + BAR_H; y++)
        for (x = BAR_X; x < BAR_X + BAR_W; x += 2) {
            int inner = y > BAR_Y && y < BAR_Y + BAR_H - 1;
            uint16_t a = (uint16_t)(inner && x > BAR_X && x - BAR_X - 1 < fill ? COL_RED : COL_FRAME);
            uint16_t b = (uint16_t)(inner && x + 1 < BAR_X + BAR_W - 1 && x - BAR_X < fill ? COL_RED : COL_FRAME);
            fb[0x100 + (y * SCR_W + x) / 2] = (uint16_t)(a << 8 | b);
        }
}

/* line table, the splash pixels and the bar into the frame buffer being drawn */
static void draw_splash(int fill)
{
    volatile uint16_t *fb = SC_FRAMEBUFFER;
    const uint16_t *px = (const uint16_t *)(splash + 512);
    int i;
    for (i = 0; i < SCR_H; i++) fb[i] = (uint16_t)(0x100 + i * (SCR_W / 2));
    for (i = 0; i < SCR_W * SCR_H / 2; i++) fb[0x100 + i] = px[i];
    draw_bar(fill);
}

int main(void)
{
    const uint16_t *pal = (const uint16_t *)splash;
    int i, fill = 0;

    SC_COMM10      = 0x0004;
    MARS_SYS_COMM0 = 0;
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    SC_COMM10 = 0x0AAA; /* "SH2 ready" */
    while (SC_COMM10 != 0x0BBB) {
    }

    /* splash into both frame buffers while the 32X layer is still off, then show it */
    take_fb();
    MARS_VDP_DISPMODE = MARS_VDP_MODE_OFF;
    for (i = 0; i < 256; i++) SC_CRAM[i] = pal[i];
    draw_splash(0);
    flip_wait();
    draw_splash(0);
    MARS_VDP_DISPMODE = (MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256; /* as Kobo: keep bit 15 */

    /* follow the progress until the Genesis picture is ready */
    for (;;) {
        uint16_t v;
        comm_pause();
        if (SC_COMM2 == GO_SIGNAL)
            break;
        comm_pause();
        v = SC_COMM12;
        if (v > BAR_IN)
            v = BAR_IN;
        if (v != fill) {
            fill = v;
            draw_bar(fill); /* the hidden buffer, show it, then the other one */
            flip_wait();
            draw_bar(fill);
        }
    }
    MARS_VDP_DISPMODE = MARS_VDP_MODE_OFF; /* the Mega Drive planes only from now on */
    while (1) {
    }
}
