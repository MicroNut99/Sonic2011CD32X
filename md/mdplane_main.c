/* PORT32X: round S3b - the Genesis 68000 plays back a recorded Palmtree Panic camera path
 * through md_planes.c with the real VDP. Loaded into Word RAM (Genesis side 0x200000) by the
 * Sub-CPU and started with the listener's 'M' command; it never returns.
 *
 * The same file builds on the PC (MDPLANE_HOST): VDP accesses go to a VDP model in
 * host/test_mdplane.c, which renders every displayed frame and compares it with the engine.
 *
 * VRAM: cells 0x0000-0xBDFF, status digits 0xBE00, plane A 0xC000, window (status row) 0xD000,
 * plane B 0xE000, sprites 0xF800 (cleared), per-line H scroll 0xFC00. H40 x V28.
 * S3c: format 2 - the PC computed every frame's scroll and name-table writes; this program only
 * copies them into the VDP in vertical blank (the 68000 was too slow doing the maths: ~17 fps).
 * S4: format 3 - the SH2 computes each frame's record live (cd32x/sh2_planes.c, md/sh2_frames.c)
 * and hands it over in the 32X frame buffer: COMM8 = record ready, COMM10 = record used.
 * Status row (top 8 lines, build with -DMDP_STATUS=1): frame, overruns (frames that missed
 * vblank), peak name words in one frame, flushes that ran past vblank into the picture. Off by default (Micronut99: no diagnostic text on screen).
 * After the first frame is on screen, 32X COMM2 = "SD" tells the SH2 to remove the splash. */
#ifndef MDP_STATUS
#define MDP_STATUS 0
#endif
#include <stdint.h>
#include "mdplane_data.h"

#ifdef MDPLANE_HOST
#define main mdplane_main
void hv_ctrl(uint16_t w);
void hv_data(uint16_t w);
unsigned hv_vline(void);
extern const uint8_t *hv_blob;
extern volatile uint32_t vint_count;
#define VDP_CTRL_W(w) hv_ctrl(w)
#define VDP_DATA_W(w) hv_data(w)
#define SIGNAL_GO()
/* S4 live mode: frame buffer, its owner bit (FM) and COMM8/COMM10, modelled by the test */
extern uint8_t hv_fb[];
void hv_fb_w16(uint32_t word, uint16_t v);
uint16_t hv_comm8_read(void);
void hv_comm10_write(uint16_t v);
#define FB_BASE ((const uint8_t *)hv_fb)
#define FB_W16(i, v) hv_fb_w16((i), (v))
void hv_fb_use(void);
#define FB_USE() hv_fb_use() /* the test checks the SH2 has handed the frame buffer over */
#define COMM8_READ() hv_comm8_read()
#define COMM10_WRITE(v) hv_comm10_write(v)
#define VLINE() hv_vline()
#define BLOB hv_blob
#define INSTALL_VINT()
#define INTS_ON()
void hv_vblank(void); /* the test renders the frame on screen, then counts the interrupt */
#define WAIT_VBLANK() hv_vblank()
#else
#define VDP_DATA (*(volatile uint16_t *)0xC00000)
#define VDP_CTRL (*(volatile uint16_t *)0xC00004)
#define VDP_HV (*(volatile uint16_t *)0xC00008)
#define VDP_CTRL_W(w) (VDP_CTRL = (uint16_t)(w))
#define VDP_DATA_W(w) (VDP_DATA = (uint16_t)(w))
#define VLINE() ((unsigned)(VDP_HV >> 8))
/* 32X COMM2 = "SD": the SH2 switches its splash layer off (cd32x/sh2_splash.c) */
#define SIGNAL_GO() (*(volatile uint16_t *)0xA15122 = 0x5344)
/* S4 live mode, Kobo's CMD 50 pattern: the SH2 hands the frame buffer over (give_fb, FM = 0) and
   takes it back (take_fb); this program only uses it in between, at 0x840000 on the Genesis side.
   COMM8 (SH2 writes): "yours" (0xDA7A for the data file, then record numbers); COMM10 (this
   program writes): the same value back when done. */
#define FB_BASE ((const uint8_t *)0x840000)
#define FB_W16(i, v) (((volatile uint16_t *)0x840000)[(i)] = (uint16_t)(v)) /* Kobo rule 4: 16-bit writes */
#define FB_USE() /* the SH2 has handed the frame buffer over (Kobo give_fb) before COMM8 says so */
#define COMM8_READ() (*(volatile uint16_t *)0xA15128)
#define COMM10_WRITE(v) (*(volatile uint16_t *)0xA1512A = (uint16_t)(v))
extern const uint8_t __blob_start[]; /* the data, appended after the program (mdplane.ld) */
#define BLOB __blob_start
extern volatile uint32_t vint_count;
extern void vint_handler(void);
/* the Sega CD BIOS's level 6 jump table entry: jmp (opcode 0x4EF9) + address at 0xFFFD08 */
#define INSTALL_VINT()                                                        \
    do {                                                                      \
        *(volatile uint16_t *)0xFFFD06 = 0x4EF9;                              \
        *(volatile uint32_t *)0xFFFD08 = (uint32_t)vint_handler;              \
    } while (0)
#define INTS_ON() __asm__ __volatile__("move.w #0x2500,%%sr" ::: "cc")
#define WAIT_VBLANK()                       \
    do {                                    \
        uint32_t c_ = vint_count;           \
        while (vint_count == c_) {          \
        }                                   \
    } while (0)
#endif

#ifndef MDPLANE_HOST
/* Our own memset / memcpy (as Kobo K21b): the program links with -nostdlib, and GCC turns
   plain clearing / copying loops (md_plane_reset) into calls to these. The volatile pointers
   stop GCC from turning THESE loops into calls to themselves. (S3b2 fix) */
#include <stddef.h>
void *memset(void *d, int c, size_t n)
{
    volatile uint8_t *p = (volatile uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    volatile uint8_t *p     = (volatile uint8_t *)d;
    const volatile uint8_t *q = (const volatile uint8_t *)s;
    while (n--) *p++ = *q++;
    return d;
}
#endif

#define RD16(p) ((uint16_t)(((const uint8_t *)(p))[0] << 8 | ((const uint8_t *)(p))[1]))
#define RD32(p) ((uint32_t)RD16(p) << 16 | RD16((const uint8_t *)(p) + 2))
#ifdef MDPLANE_HOST
#define W16(p) RD16(p)
#else
#define W16(p) (*(const uint16_t *)(p)) /* one 16-bit read: the vblank copy loops need the speed */
#endif

#define NAME_A 0xC000
#define NAME_W 0xD000
#define NAME_B 0xE000
#define SPRITES 0xF800
#define HSCROLL 0xFC00
#define LINES_MAX 240

static void vram_addr(uint16_t a)
{
    VDP_CTRL_W(0x4000 | (a & 0x3FFF));
    VDP_CTRL_W(a >> 14);
}
static void cram_addr(uint16_t a)
{
    VDP_CTRL_W(0xC000 | (a & 0x3FFF));
    VDP_CTRL_W(0);
}
static void vsram_addr(uint16_t a)
{
    VDP_CTRL_W(0x4000 | (a & 0x3FFF));
    VDP_CTRL_W(0x0010);
}

static const uint8_t hexfont[16][8] = {
    { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0 }, { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0 },
    { 0x3C, 0x66, 0x06, 0x1C, 0x30, 0x60, 0x7E, 0 }, { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0 },
    { 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0 }, { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0 },
    { 0x3C, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C, 0 }, { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0 },
    { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0 }, { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0 },
    { 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0 }, { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0 },
    { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0 }, { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0 },
    { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0 }, { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0 },
};

static int16_t hsTab[2 * LINES_MAX]; /* the H scroll table as the VDP wants it: A, B per line */
static int hasB, lines;

/* expand runs of (lines, value) into every other entry of the table */
static const uint8_t *expand_runs(const uint8_t *p, int16_t *dst)
{
    int n = RD16(p), k, y = 0;
    p += 2;
    for (k = 0; k < n; k++) {
        int cnt = W16(p);
        int16_t v = (int16_t)W16(p + 2);
        p += 4;
        while (cnt-- > 0 && y < lines) {
            dst[2 * y] = v;
            y++;
        }
    }
    return p;
}

static void put_hex(uint16_t *row, int at, uint16_t v, uint16_t base)
{
    int i;
    for (i = 0; i < 4; i++) row[at + i] = (uint16_t)(base + ((v >> (12 - 4 * i)) & 15));
}

int main(void)
{
    const uint8_t *b = BLOB;
    const uint8_t *frames, *fp;
    uint16_t nframes, ncells, statusLine, statusSlot, statusBase = 0;
    uint16_t cram0[64];
    uint16_t status[64];
    uint16_t frameNo = 0, overruns = 0, peakWords = 0, spills = 0;
    uint32_t lastFlush = 0;
    int i, t, showStatus, live;
    uint16_t id = 0;

    VDP_CTRL_W(0x8104); /* display off first: a black screen = this program started (S3b4) */
    /* format 2 (S3c): every frame precomputed in Word RAM; format 3 (S4): the SH2 computes live */
    if (RD32(b) != MDP_MAGIC || (RD16(b + 4) != 2 && RD16(b + 4) != 3))
        for (;;) {
        }
    live = RD16(b + 4) == 3;
    lines      = RD16(b + 6);
    nframes    = RD16(b + 10);
    ncells     = RD16(b + 16);
    statusLine = RD16(b + 64);
    statusSlot = RD16(b + 66);
    hasB       = RD16(b + 52) != 0;
    showStatus = MDP_STATUS && statusLine != 0xFFFF;
    if (lines > LINES_MAX)
        lines = LINES_MAX;
    frames = b + RD32(b + 32);

    /* ---- VDP set-up, display off ---- */
    {
        static const uint16_t regs[] = { 0x8004, 0x8104, 0x8230, 0x8334, 0x8407, 0x857C, 0x8600, 0x8700, 0x8800, 0x8900,
                                         0x8AFF, 0x8B03, 0x8C81, 0x8D3F, 0x8E00, 0x8F02, 0x9001, 0x9100, 0x9200 };
        for (i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); i++) VDP_CTRL_W(regs[i]);
        if (showStatus)
            VDP_CTRL_W(0x9201); /* window: the top cell row */
    }
    vram_addr(0);
    for (i = 0; i < 0x8000; i++) VDP_DATA_W(0);
    vsram_addr(0);
    for (i = 0; i < 40; i++) VDP_DATA_W(0);
    {
        const uint8_t *c = b + RD32(b + 12);
        vram_addr(0);
        for (i = 0; i < ncells * 16; i++) VDP_DATA_W(RD16(c + 2 * i));
    }
    if (showStatus) {
        statusBase = (uint16_t)(0x8000 | statusLine << 13 | MDP_STATUS_CELL);
        vram_addr(MDP_STATUS_CELL * 32);
        for (t = 0; t < 16; t++)
            for (i = 0; i < 8; i++) {
                uint8_t bits = hexfont[t][i];
                uint16_t w0 = 0, w1 = 0;
                int k;
                for (k = 0; k < 4; k++) {
                    if (bits & (0x80 >> k))
                        w0 |= (uint16_t)(statusSlot << (12 - 4 * k));
                    if (bits & (0x08 >> k))
                        w1 |= (uint16_t)(statusSlot << (12 - 4 * k));
                }
                VDP_DATA_W(w0);
                VDP_DATA_W(w1);
            }
    }
    {
        const uint8_t *c = b + RD32(b + 28);
        for (i = 0; i < 64; i++) cram0[i] = RD16(c + 2 * i);
        if (showStatus)
            cram0[statusLine * 16 + statusSlot] = 0x0EEE;
        cram_addr(0);
        for (i = 0; i < 64; i++) VDP_DATA_W(cram0[i]);
    }
    for (i = 0; i < 64; i++) status[i] = 0;
    for (i = 0; i < 2 * LINES_MAX; i++) hsTab[i] = 0;

    VDP_CTRL_W(0x8124); /* display still off, vertical blank interrupt on (main() waits on it) */
    INSTALL_VINT();
    INTS_ON();

    if (live) { /* the whole data file into the frame buffer for the SH2 (as Kobo's CMD 50) */
        uint32_t n = (RD32(b + 60) + 1) / 2, k;
        while (COMM8_READ() != MDP_DATA_READY) { /* the SH2 cleared it and handed it over */
            volatile int p;
            for (p = 0; p < 40; p++) {
            }
        }
        FB_USE();
        for (k = 0; k < n; k++) FB_W16(k, W16(b + 2 * k));
        COMM10_WRITE(MDP_DATA_READY); /* "data in the frame buffer" */
    }

    /* ---- play the recording, looping. During the picture: only unpack this frame's
       scroll; in vertical blank: copy writes, scroll and palette changes into the VDP ---- */
    fp = frames;
    for (;;) {
        int restart;
        uint16_t vsA, vsB;
        const uint8_t *groups, *cram;
        int ngroups, ncram, words = 0, big;
        if (live) { /* wait for the SH2's record (it hands the frame buffer over with it) */
            id = (uint16_t)MDP_NEXT_ID(id);
            while (COMM8_READ() != id) {
                volatile int k;
                for (k = 0; k < 40; k++) { /* Kobo HW6: pause between COMM reads */
                }
            }
            FB_USE();
            fp      = FB_BASE;
            restart = id == 1;
        }
        else
            restart = fp == frames;
        if (restart)
            frameNo = 0;
        vsA = RD16(fp);
        vsB = RD16(fp + 2);
        fp  = expand_runs(fp + 4, hsTab);
        fp  = expand_runs(fp, hsTab + 1);
        ngroups = RD16(fp);
        groups  = fp + 2;
        fp += 2;
        for (i = 0; i < ngroups; i++) {
            int cnt = RD16(fp + 2);
            words += cnt;
            fp += 4 + 2 * cnt;
        }
        ncram = RD16(fp);
        cram  = fp + 2;
        fp += 2 + 4 * ncram;
        if (words > peakWords)
            peakWords = (uint16_t)words;
        big = restart || words > 600; /* a big reload: blank one frame rather than tear */
        if (showStatus) {
            put_hex(status, 1, frameNo, statusBase);
            put_hex(status, 6, overruns, statusBase);
            put_hex(status, 11, peakWords, statusBase);
            put_hex(status, 16, spills, statusBase);
        }

        /* wait for vblank; if one already passed since the last copy, this frame was late */
        if (!restart && vint_count != lastFlush)
            overruns++;
        WAIT_VBLANK();
        {
            const uint8_t *g = groups;
            if (big)
                VDP_CTRL_W(0x8124); /* display off, vertical blank interrupt stays on */
            for (i = 0; i < ngroups; i++) {
                int cnt = W16(g + 2), k;
                const uint16_t *w = (const uint16_t *)(g + 4);
                vram_addr(W16(g));
                for (k = 0; k < cnt; k++) VDP_DATA_W(W16(w + k));
                g += 4 + 2 * cnt;
            }
            vram_addr(HSCROLL);
            {
                const int16_t *h = hsTab;
                int n = 2 * lines;
                while (n-- > 0) VDP_DATA_W((uint16_t)*h++);
            }
            vsram_addr(0);
            VDP_DATA_W(vsA);
            VDP_DATA_W(hasB ? vsB : 0);
            if (restart && !live) {
                cram_addr(0);
                for (i = 0; i < 64; i++) VDP_DATA_W(cram0[i]);
            }
            for (i = 0; i < ncram; i++) {
                uint16_t idx = RD16(cram + 4 * i);
                if (showStatus && idx == statusLine * 16 + statusSlot)
                    continue;
                cram_addr((uint16_t)(idx * 2));
                VDP_DATA_W(RD16(cram + 4 * i + 2));
            }
            if (showStatus) {
                vram_addr(NAME_W);
                for (i = 0; i < 20; i++) VDP_DATA_W(status[i]);
            }
            if (big)
                VDP_CTRL_W(0x8164); /* display on */
            else if (VLINE() < 224)
                spills++; /* the copying ran past the vertical blank into the picture */
        }
        lastFlush = vint_count;
        if (live) /* record used: the SH2 takes the frame buffer back */
            COMM10_WRITE(id);
        if (restart && frameNo == 0)
            SIGNAL_GO(); /* the first picture is ready: splash off (repeating it is harmless) */
        frameNo++;
        if (!live && frameNo >= nframes)
            fp = frames;
    }
}
