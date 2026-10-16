/* PORT32X: S4 - the master SH2 computes Palmtree Panic's planes live (from the recorded camera
 * path, until the engine itself runs here) and hands each frame's record to the 68000.
 *   - start handshake (COMM10 0x0AAA / 0x0BBB, as Kobo), 32X layer off (Genesis picture only)
 *   - frame buffer cleared (Kobo K14: zero words written by the 68000 may not land), handed to
 *     the 68000 (give_fb, Kobo), COMM8 = 0xDA7A; the 68000 copies MDPLANE's data file in and
 *     answers COMM10 = 0xDA7A; take_fb (Kobo); the file is copied to SDRAM
 *   - per frame: md/sh2_frames.c builds the record, written into the frame buffer (16-bit, Kobo
 *     rule 4), give_fb, COMM8 = its number; the 68000 copies it into the VDP in vertical blank and
 *     answers COMM10 = the number; take_fb - then the next record.
 * Exactly Kobo's CMD 50 pattern: only the SH2 switches FM (give_fb / take_fb, waiting for the
 * bit to read back), the 68000 only uses the frame buffer while it is handed over.
 * One writer per COMM register (this program: COMM8; the 68000: COMM10), pauses (Kobo HW6). */
#include <stddef.h>
#include <stdint.h>
#include "32x.h"
#include "../md/sh2_frames.h"
#include "../md/mdplane_data.h"

const char sh2_build_marker[] = "SH2-VS4L"; /* the Sub-CPU looks for "SH2-V" */

#define SC_COMM8 (*(volatile uint16_t *)0x20004028)
#define SC_COMM10 (*(volatile uint16_t *)0x2000402A)
#define SC_COMM12 (*(volatile uint16_t *)0x2000402C)
#define SC_CMD_CLR (*(volatile uint16_t *)0x2000401A)
#define SC_SYS_ADAPTER_B (*(volatile uint8_t *)0x20004000)
#define SC_FRAMEBUFFER ((volatile uint16_t *)0x24000000)
#define SH2_OWNS_FB() (MARS_SYS_INTMSK & 0x8000) /* FM = 1 (Kobo take_fb waits on this bit) */

/* SDRAM layout (Kobo: program from 0x06000000, master stack up to 0x0603F400) */
#define PROGRAM_END 0x06008000
#define DATA ((uint8_t *)0x06008000)          /* the data file, up to 128 KB */
#define DATA_MAX 0x20000
#define W_ADDR ((uint16_t *)0x06028000)       /* name-table write list (4096 entries) */
#define W_WORD ((uint16_t *)0x0602A000)
#define RECORD ((uint16_t *)0x0602C000)       /* one record, up to 0x4000 words */
#define RECORD_MAX 0x4000

/* the linker's __bss_end: sh-elf puts '_' before C names (crt0.s calls _main for main) */
extern char _bss_end[];

void pri_vbi_handler(void) {}
void pri_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_dma1_handler(void) {}
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;

/* our own memset / memcpy (Kobo K21b): linked with -nostdlib; volatile so GCC cannot turn these
   loops into calls to themselves */
void *memset(void *d, int c, size_t n)
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

static void give_fb(void) /* Kobo */
{
    SC_SYS_ADAPTER_B = 0;
    while ((MARS_SYS_INTMSK & 0x8000) != 0) {
    }
}

static void wait_comm10(uint16_t v) /* Kobo HW6: slow polling */
{
    while (SC_COMM10 != v) {
        volatile int k;
        for (k = 0; k < 64; k++) {
        }
    }
}

static void comm_pause(void) /* Kobo HW6: ~20 us between COMM reads */
{
    volatile int k;
    for (k = 0; k < 64; k++) {
    }
}

static void stop(uint16_t code) /* COMM12 = 0x4Bxx style code (Kobo's STATE convention), then idle */
{
    SC_COMM12 = code;
    while (1) {
    }
}

static Sh2Frames S;

int main(void)
{
    uint16_t id = 0;
    uint32_t size, k;

    SC_COMM10      = 0x0004;
    SC_COMM8       = 0; /* COMM8 = "record ready": nothing yet (it can hold anything at power-on) */
    MARS_SYS_COMM0 = 0;
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    SC_COMM10 = 0x0AAA; /* "SH2 ready" */
    while (SC_COMM10 != 0x0BBB) {
    }
    take_fb();
    MARS_VDP_DISPMODE = MARS_VDP_MODE_OFF; /* the Mega Drive planes only */
    if ((uint32_t)_bss_end > PROGRAM_END)
        stop(0x4BE6); /* the program grew into the data area (Kobo used 4BE6 for "program too big") */

    /* the data file: clear the frame buffer (K14), hand it over, the 68000 copies the file in */
    {
        volatile uint16_t *fb = SC_FRAMEBUFFER;
        for (k = 0; k < 0x10000; k++) fb[k] = 0;
    }
    give_fb();
    SC_COMM8 = MDP_DATA_READY; /* "the frame buffer is yours: copy the file in" */
    wait_comm10(MDP_DATA_READY);
    take_fb();
    {
        volatile uint16_t *fb = SC_FRAMEBUFFER;
        uint16_t *dst         = (uint16_t *)DATA;
        size                  = (uint32_t)fb[30] << 16 | fb[31]; /* header byte 60: total size */
        if (size > DATA_MAX)
            stop(0x4BE5);
        for (k = 0; k < (size + 1) / 2; k++) dst[k] = fb[k];
    }
    if (sh2f_init(&S, DATA, (const uint16_t *)DATA, W_ADDR, W_WORD))
        stop(0x4BE3); /* not a format-3 file */

    /* the record loop (the frame buffer is ours here) */
    for (;;) {
        int n = sh2f_next(&S, RECORD, RECORD_MAX);
        if (n < 0)
            stop(0x4BE2);
        {
            volatile uint16_t *fb = SC_FRAMEBUFFER;
            int i;
            for (i = 0; i < n; i++) fb[i] = RECORD[i];
        }
        give_fb();
        id       = (uint16_t)MDP_NEXT_ID(id);
        SC_COMM8 = id; /* "record ready, the frame buffer is yours" */
        wait_comm10(id);
        take_fb();
    }
}
