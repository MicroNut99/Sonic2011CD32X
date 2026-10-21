// PORT32X S7b: the SH2 platform layer of the engine (engine/plat.hpp) - running from the cart.
// What the boot loader (cd32x/sh2_engboot.c) left: the engine image at cart 0x000000, the stage's
// console cart image at 0x080000, its mini data pack at 0x180000, and a boot record (lengths) at
// SDRAM 0x06001F00. The frame buffer (SH2-owned): record area 12 KB (here: the status box and its
// line table) | menus 34 KB | layout pool 82 KB.
#include "../../engine/RetroEngine.hpp"
#include "32x.h"
extern "C" {
#include "../../md/cart_verify.h"
#if SCD_PLANES
#include "../../md/md_planes.h"
#include "../../md/sgb.h"
#include "../../md/md_sprites.h"
#include "../../md/md_sprites.c" /* REV R3: compiled here (no Makefile change) */
#endif
}

#define CART_STAGE ((const unsigned char *)0x02080000) /* cached: the cart is read-only */
#define CART_PACK ((const unsigned char *)0x02300000) /* S7e: the data pack at 3 MB (the scene slot grew) */
#define SCD_BOOT_RECORD ((volatile unsigned int *)0x06001F00) /* "SCDB", engine, stage, pack bytes */
#define SCD_HEAP_LIMIT 0x0603D400u                            /* the master stack (8 KB, R1W) starts here (S7b3) */
#define SC_VDP_FBCTL (*(volatile unsigned short *)0x2000410A)
#define SC_CRAM ((volatile unsigned short *)0x20004200)
#define SC_FB ((volatile unsigned short *)0x24000000)
#define VBLANK_COUNT (*(volatile unsigned int *)0x06001F10) /* S7b5: counted by the loader's VBI */
#ifndef SCD_SCENE_CHECK
#define SCD_SCENE_CHECK 0 /* R1D */
#endif
#ifndef SCD_DEVMENU
#define SCD_DEVMENU 1 /* REV R4: the game starts in the dev menu (engine/Debug.cpp: level select, test switches) */
#endif
/* R1C: the loader's PC sampler (sh2_engboot.c, boot record [7] = &scdSampling). On, every 30 vertical
   blanks line 10 shows where the SH2 is: 8xxx = cart code 0x02000000 + xxx * 16, 7xxx = SDRAM. Only on
   while nothing waits for a COMM12 reply (scene loads and SH2 command 68 use COMM12). */
static void scd_sampling(unsigned int on)
{
    unsigned int a = SCD_BOOT_RECORD[7];
    if (a >= 0x06000000u && a < 0x06002000u)
        *(volatile unsigned int *)a = on;
}
static unsigned int scd_sampling_now() /* REV R4C: the sampler on (1) - it writes COMM8's high word and COMM12 */
{
    unsigned int a = SCD_BOOT_RECORD[7];
    return (a >= 0x06000000u && a < 0x06002000u) ? *(volatile unsigned int *)a : 0;
}
#define STATE(code) (*(volatile unsigned short *)0x2000402C = (unsigned short)(code)) /* Kobo's STATE (COMM12) */

void scd_status(int row, const char *t, int color);
void scd_text_on(); /* REV R4B: the Genesis display on (the loading picture off) - below */
// S7e7: defined HERE, before its first use in plat_present (S7e4-S7e6 defined it further down, so
// plat_present's readings block was silently compiled out)
#ifndef SCD_DRAW_DIAG
#define SCD_DRAW_DIAG 1 /* the drawing test disc - Genesis text stays on, line 10 shows the readings */
#endif
static unsigned int diagUntouched = 0; // S7e7: here too (used in plat_present)

// ---------------------------------------------------------------- the status box (32X layer)
// Line table: rows 0-207 show one transparent line, rows 208-223 the box - all inside the first
// 6 KB of the frame buffer (the record area), so menus and the layout pool are never touched.
// Kobo's 3x5 font, 16-bit writes (Kobo rule 4). The box sits bottom right (x >= 208) so Chilly's
// white and Kobo's green start-up lines stay readable.
static const unsigned char font3x5[][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
    { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
    { 2, 5, 7, 5, 5 }, { 6, 5, 6, 5, 6 }, { 3, 4, 4, 4, 3 }, { 6, 5, 5, 5, 6 }, { 7, 4, 7, 4, 7 },
    { 7, 4, 7, 4, 4 }, { 5, 5, 6, 5, 5 }, { 4, 4, 4, 4, 7 }, { 5, 7, 7, 5, 5 }, { 6, 5, 6, 5, 5 },
    { 5, 5, 5, 5, 2 }, { 0, 0, 0, 0, 0 }, { 7, 2, 2, 2, 2 }, { 5, 5, 2, 5, 5 }, { 5, 5, 2, 2, 2 },
    { 7, 4, 5, 5, 7 }, { 5, 5, 7, 5, 5 }, { 7, 2, 2, 2, 7 }, { 5, 7, 7, 5, 5 }, { 7, 5, 5, 5, 7 },
    { 7, 5, 7, 4, 4 }, { 5, 5, 5, 5, 7 }, { 5, 5, 7, 7, 5 }
};
static int glyph(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 32);
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    switch (c) {
        case 'K': return 16; case 'L': return 17; case 'N': return 18; case 'R': return 19; case 'S': return 5;
        case 'V': return 20; case 'T': return 22; case 'X': return 23; case 'Y': return 24; case 'G': return 25;
        case 'H': return 26; case 'I': return 27; case 'M': return 28; case 'O': return 29; case 'P': return 30;
        case 'U': return 31; case 'W': return 32;
    }
    return 21;
}
enum { PX_CLEAR = 0, PX_BOX = 1, PX_GREEN = 2, PX_RED = 3, PX_WHITE = 4 };
#define BOX_X 208
#define BOX_Y 196        /* S7b4: double-size text - two lines of 10 pixels, 14 characters each */
#define BOX_ROWS (224 - BOX_Y)
#define TEXT_WORDS 0x100 /* rows 196-223: 28 x 160 words */
#define CLEAR_LINE (TEXT_WORDS + BOX_ROWS * 160)
static bool statusReady = false;
static void status_frame()
{
    volatile unsigned short *fb = SC_FB;
    for (int y = 0; y < 224; ++y) fb[y] = (unsigned short)(y < BOX_Y ? CLEAR_LINE : TEXT_WORDS + (y - BOX_Y) * 160);
    for (int i = 0; i < 160; ++i) fb[CLEAR_LINE + i] = 0;
    for (int r = 0; r < BOX_ROWS; ++r)
        for (int i = 0; i < 160; ++i) fb[TEXT_WORDS + r * 160 + i] = (unsigned short)((i * 2 >= BOX_X) ? (PX_BOX << 8 | PX_BOX) : 0);
}
static void flip_wait()
{
    unsigned short cur = SC_VDP_FBCTL & 1;
    SC_VDP_FBCTL       = cur ^ 1;
    while ((SC_VDP_FBCTL & 1) == cur) {
    }
}
static void status_init()
{
    if (statusReady)
        return;
    statusReady = true;
    MARS_VDP_DISPMODE = (MARS_VDP_DISPMODE & 0x8000); /* REV R4B: blank first (a picture may be up): the palette can be
                                                        written at any time then */
    SC_CRAM[PX_CLEAR] = 0x0000; /* see-through */
    SC_CRAM[PX_BOX]   = 0x0400; /* black (Kobo: never 0x0000) */
    SC_CRAM[PX_GREEN] = 0x03E0;
    SC_CRAM[PX_RED]   = 0x001F;
    SC_CRAM[PX_WHITE] = 0x7FFF;
    status_frame();
    flip_wait();
    status_frame();
    MARS_VDP_DISPMODE = (MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256;
}
static void put_text(int x, int y, const char *t, int c) /* S7b4: each font pixel 2 x 2 */
{
    volatile unsigned short *fb = SC_FB;
    for (int n = 0; n < 13; ++n, x += 8) {
        const unsigned char *g = font3x5[*t ? glyph(*t) : 21];
        for (int r = 0; r < 10; r++) {
            int bits              = g[r / 2];
            int p0 = (bits & 4) ? c : PX_BOX, p1 = (bits & 2) ? c : PX_BOX, p2 = (bits & 1) ? c : PX_BOX;
            int w                 = TEXT_WORDS + (y + r) * 160 + x / 2;
            fb[w]                 = (unsigned short)(p0 << 8 | p0);
            fb[w + 1]             = (unsigned short)(p1 << 8 | p1);
            fb[w + 2]             = (unsigned short)(p2 << 8 | p2);
            fb[w + 3]             = (unsigned short)(PX_BOX << 8 | PX_BOX);
        }
        if (*t)
            t++;
    }
}
void scd_status(int row, const char *t, int color) /* row 0 / 1, 13 characters; into both frame buffers */
{
    status_init();
    for (int k = 0; k < 2; ++k) {
        put_text(BOX_X + 4, 2 + row * 13, t, color);
        flip_wait();
    }
}
static char *hex(char *p, unsigned v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) {
        int n = v & 15;
        p[i]  = (char)(n < 10 ? '0' + n : 'A' + n - 10);
        v >>= 4;
    }
    p[digits] = 0;
    return p + digits;
}

// ---------------------------------------------------------------- the platform API
// S7b5: time = frames counted by the vertical-blank interrupt (as Kobo: interrupts on)
unsigned int plat_ticks_ms() { return VBLANK_COUNT * 1000 / 60; }
static unsigned int presented = 0;
int scdDemoPad                    = 0; /* REV R4: the dev menu's DEMO PAD switch (engine/plat.hpp) */
static unsigned int scdDemoFrom   = 0; /* the frame the stage started: the scripted pad starts from it */

#if SCD_PLANES
// ---------------------------------------------------------------- REV R1: the Genesis planes
// The Genesis VDP draws the level (both tile layers, per-line parallax); the 32X layer is blank.
// Each scene with a G<list>S<pos>.MDG on the disc (the act's Genesis data, md/sgb.h) is loaded into
// cart 0x380000; the Sub-CPU sets the VDP up from it (SH2 command 68). Each frame the engine's own
// layer scroll (plat_layer_*) is streamed by md_planes.c (the code proven against the engine on the
// PC) into a record in the frame buffer's record area; the 68000 takes it (COMM4 id / COMM6 answer,
// Kobo's frame-buffer pattern: only the SH2 switches FM) and writes it into the VDP in vertical blank.
// Status codes: 5C61 no MDG for this scene (the 32X/text as before), 5C62 MDG header bad, 5C63 the
// Sub-CPU refused it, 5C64 the 68000 did not take a record (planes switched off).
static unsigned int sgbOff = 0x380000u; /* REV R4G: where the act file is, in window 1: 0x380000 (page 1, the 4 MB
                                           cart's place), or a slot of the 16 MB cart's page 7 (THE PRELOAD, below) */
#define SGB_CART_OFFSET sgbOff
#define SGB_FILE ((const unsigned char *)(0x02000000u + sgbOff)) /* cached: purged after every load and page switch */
#define SGB_FB_REC ((unsigned short *)0x24000000)  /* the frame buffer's 12 KB record area */
#define SC_COMM0 (*(volatile unsigned short *)0x20004020)
#define SC_COMM2 (*(volatile unsigned short *)0x20004022)
#define SC_COMM4 (*(volatile unsigned short *)0x20004024)
#define SC_COMM6 (*(volatile unsigned short *)0x20004026)
#define SC_COMM8_32 (*(volatile unsigned int *)0x20004028)
#define SC_COMM12 (*(volatile unsigned short *)0x2000402C)
#define SC_ADAPTER_B (*(volatile unsigned char *)0x20004000)
typedef int (*ScdLoadFile)(unsigned short number, unsigned int *bytes); /* the loader's resident routine */
static bool planesOn = false;
/* REV R4C: the act's animated tiles (the scripts' Copy16x16Tile; md/sgb.h 'ANI1'): the planes show tile t with the name
   words of the tile (as loaded) whose pixels it has now - pmTileMap, the picture mode's own map (scd_pm.h); a tile
   that changed (aniDirty) has its cells the planes hold written again (planes_frame, md_plane_refresh) */
static unsigned short pmTileMap[TILE_COUNT];
#define PM_TILE_MAP_OWN 1
static const unsigned char *aniTiles = 0; /* the act file's section: 128 bytes the tiles with name words, 64 the chunks
                                             that hold an animated tile (0: the act has none / not a planes act) */
static unsigned int aniDirty[32];         /* 1024 bits (md_planes.h tileBits) */
static bool aniDirtyAny = false;
static MdPlane sgbA, sgbB;
static int sgbBLayer = -1;
static bool slotUsed[4];
static unsigned char slotLayer[4];
static int tileYA, tileYB, frameBg;
/* REV R4C: the planes' scroll arrays share their SDRAM with the 32X picture mode's (below): never used together */
static union {
    struct {
        int xA[SCREEN_YSIZE], xB[SCREEN_YSIZE];
        int16_t hsA[SCREEN_YSIZE], hsB[SCREEN_YSIZE];
    } pl;
    struct {
        int lines[SCREEN_YSIZE];      /* a tile layer's x per line (DrawHLineScrollLayer's own) */
        DrawListEntry3D list[0xA0];   /* the 3D scene's faces, sorted */
        unsigned short fIdx[32], fGen[32], fSent[32]; /* REV R5E: the chip's picture's colours (R5: the floor's 16) */
    } pm;
} scdShared;
#define xA scdShared.pl.xA
#define xB scdShared.pl.xB
#define hsA scdShared.pl.hsA
#define hsB scdShared.pl.hsB
#define pmLines scdShared.pm.lines
#define pmList scdShared.pm.list
#define flrIdx scdShared.pm.fIdx
#define flrGen scdShared.pm.fGen
#define flrSent scdShared.pm.fSent
static unsigned short cramSent[64];
static unsigned short recId = 0;
/* REV R4D: the UNDERWATER switch (scdWaterSwitch) - the records' water trailer (md/sgb.h): the water colours the 68000
   has (waterSent; 0xFFFF: none yet), and the first record of an act with all 64 normal colours (cramSync: the 68000's
   shadow of CRAM, which it writes back after the water line) */
static unsigned short waterSent[64];
static bool cramSync = false;
static uint16_t wIdx[64], wCol[64];
static int wN = 0;
/* the first line drawn with another palette bank than line 0's, if every line below it has that bank (one water line);
   0: none - a frame with more than one change (as R4C: line 0's colours everywhere) */
static int water_line()
{
    const int b0 = gfxLineBuffer[0];
    int y        = 1;
    while (y < SCREEN_YSIZE && gfxLineBuffer[y] == b0) ++y;
    if (y >= SCREEN_YSIZE)
        return 0;
    const int wb = gfxLineBuffer[y];
    for (int k = y + 1; k < SCREEN_YSIZE; ++k)
        if (gfxLineBuffer[k] != wb)
            return 0;
    return y;
}
/* the trailer: the water line and the water colours that changed (the CRAM slots' engine colours in the water line's
   bank, faded as the normal ones; a slot without one: its normal colour). dim: the pause menu's (lines 0-2 at 40 %) */
static int water_out(SgbOut *o, const unsigned short *slots, bool dim)
{
    wN    = 0;
    int W = water_line();
    if (W) {
        const unsigned short *pal = fullPalette[gfxLineBuffer[W]];
        for (int i = 0; i < 64; ++i) {
            uint16_t c = cramSent[i];
            if (slots[i] != 0xFFFF) {
                uint16_t c565 = pal[slots[i]];
                if (dim) {
                    if (i < 48)
                        c565 = sgb_fade565(c565, 0, 0, 0, 150);
                }
                else if (fadeMode > 0)
                    c565 = sgb_fade565(c565, fadeR, fadeG, fadeB, fadeA);
                c = sgb_md_color(c565);
            }
            if (c != waterSent[i]) {
                wIdx[wN]     = (uint16_t)i;
                wCol[wN++]   = c;
                waterSent[i] = c;
            }
        }
    }
    return sgb_out_water(o, W, wIdx, wCol, wN);
}
/* a record that did not fit (not sent): its colour changes again next frame (REV R4D - R4C lost them until the colour
   changed again) */
static void colours_lost(const unsigned short *slots, const uint16_t *idx, int n)
{
    for (int k = 0; k < n; ++k)
        if (slots[idx[k]] != 0xFFFF)
            cramSent[idx[k]] = 0xFFFF;
    for (int k = 0; k < wN; ++k) waterSent[wIdx[k]] = 0xFFFF;
    wN = 0;
}
static MdSprites sprites; /* REV R3: the act's sprites as Genesis hardware sprites (md/md_sprites.h) */
static void sgb_pause()
{
    for (volatile int k = 0; k < 64; ++k) {
    } /* Kobo HW6: pause between COMM reads */
}
static void comm_idle();
static bool genesisOff = true; /* REV R4B: the Genesis display may be off (command 52; at power-on the boot loader's) */
static int sgb_cmd(unsigned short op, unsigned int arg) /* SH2 command 68 -> reply (COMM12) */
{
    comm_idle(); /* REV R4: the pad request answered first */
    SC_COMM2    = op;
    SC_COMM8_32 = arg;
    SC_COMM12   = 0xFFFF;
    SC_COMM0    = 68;
    for (unsigned int n = 0; n < 2000000u && SC_COMM0 != 0; ++n) sgb_pause();
    int r = SC_COMM12;
    if ((op == 1 && r == 1) || op == 2)
        genesisOff = false; /* REV R4B: both leave the Genesis display on (cd32x/sub_main.c) */
    return r;
}
static void sgb_give_fb() /* Kobo give_fb: FM = 0, wait until it reads 0 */
{
    SC_ADAPTER_B = 0;
    while ((MARS_SYS_INTMSK & 0x8000) != 0) {
    }
}
static void sgb_take_fb() /* Kobo take_fb */
{
    SC_ADAPTER_B = 0x80;
    while ((MARS_SYS_INTMSK & 0x8000) == 0) {
    }
}
static bool menuOn = false; /* REV R4: the dev menu's text on plane A (below) */
static void fb_clear(unsigned int at, int bytes);
/* REV R4B: layout entries the scripts changed (plat_layout_set, from SetTileLayerEntry): after the frame's plane update
   their cells that the planes show now are written again (md_plane_chunk); more than the queue holds in one frame -
   both planes from scratch. A tile trigger changes four entries at once. */
#define LAYOUT_QUEUE 8 /* (SDRAM: 24 bytes) */
static unsigned char layoutQ[LAYOUT_QUEUE][3];
static int layoutQn          = 0;
static bool layoutQLost      = false;
void plat_layout_set(int layer, int x, int y)
{
    if (layoutQn >= LAYOUT_QUEUE)
        layoutQLost = true;
    else {
        layoutQ[layoutQn][0] = (unsigned char)layer;
        layoutQ[layoutQn][1] = (unsigned char)x;
        layoutQ[layoutQn][2] = (unsigned char)y;
        layoutQn++;
    }
}
// ---------------------------------------------------------------- REV R4B: the 32X sky
// An act whose file has a sky section (md/sgb.h 'SKY1', host/md_check.cpp): its background's sky colour is see-through
// in the Genesis tiles, and the 32X layer behind the Genesis picture shows a gradient there. The frame buffer the 32X
// shows holds a fixed picture: screen line pair g in colours 16 + 2g and 17 + 2g, a 2 x 2 checkerboard; the engine
// keeps its data in the other buffer (the one the SH2 and the 68000 use) as before. Each vertical blank the SH2 sets
// the 112 pairs' colours from the background row at the top of the screen - the gradient moves with the background.
// The colours have no bit 15: behind the Genesis (32X priority bit clear), seen where its planes and sprites are
// see-through. The engine's fade and changes of the sky colour itself apply to them as to the Genesis palette.
#define SKY_BASE 16 /* 32X colours 16-239 (0-15: the status box) */
static const unsigned short *skyPairs = 0; /* the act file's table (the cart): pairs x { A, B } */
static int skyPairsN = 0, skyLayer = -1, skyIdx = 144, skyY = -1;
static unsigned short sky565  = 0;     /* the sky colour (RGB565) the table was made for */
static bool skyPicture        = false; /* the shown frame buffer holds the sky picture */
static bool skyShown          = false; /* the 32X layer shows it */
static unsigned int skyLastRow = 0, skyLastFade = 0; /* what the 32X colours hold now (row and sky colour, fade);
                                                        skyLastFade 0: to be written */
static void sky_picture() /* the picture into the other frame buffer: swap, write, swap back */
{
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* blank while the engine's buffer is shown */
    flip_wait();
    volatile unsigned short *fb = SC_FB;
    for (int y = 0; y < 256; ++y) fb[y] = (unsigned short)(0x100 + (y < 224 ? y : 223) * 160);
    for (int y = 0; y < 224; ++y) {
        unsigned short a = (unsigned short)(SKY_BASE + 2 * (y >> 1)), b = (unsigned short)(a + 1);
        unsigned short w            = (y & 1) ? (unsigned short)(b << 8 | a) : (unsigned short)(a << 8 | b);
        volatile unsigned short *ln = fb + 0x100 + y * 160;
        for (int x = 0; x < 160; ++x) ln[x] = w;
    }
    flip_wait(); /* the engine's buffer is the SH2's again; the sky picture is the shown one */
    skyPicture = true;
}
static void sky_off()
{
    if (skyShown)
        MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000);
    skyShown    = false;
    skyY        = -1;
    skyLastFade = 0;
}
static void sky_load(const unsigned char *f) /* from planes_load: the act file's sky, its picture ready */
{
    sky_off();
    skyPairs = 0;
    if (*(const unsigned int *)(f + 48) != SGB_SKY_TAG)
        return;
    const unsigned short *k = (const unsigned short *)(f + *(const unsigned int *)(f + 52));
    if (k[3] < 112 || k[2] != 2 * k[3]) /* (a background of 224 rows at least: one wrap a frame) */
        return;
    skyLayer  = k[0];
    skyIdx    = k[1];
    skyPairsN = k[3];
    sky565    = k[4];
    skyPairs  = k + 6;
    if ((unsigned int)(unsigned long)skyPairs & 3) { /* (md_check.cpp aligns it: sky_vblank reads a pair at a time) */
        skyPairs = 0;
        return;
    }
    if (!skyPicture)
        sky_picture();
}
static inline int sky_div255(int x) { return (x * 257 + 257) >> 16; } /* x / 255 (0 <= x <= 255 * 255) */
static void sky_vblank() __attribute__((noinline)); /* cart code: not inlined into plat_present (SDRAM is full) */
static void sky_vblank() /* in the vertical blank, after the frame's record went to the 68000 (plat_present) */
{
    if (!skyPairs || !skyPicture || skyY < 0) {
        if (skyShown)
            sky_off();
        return;
    }
    /* the sky colour's own changes (in 5-bit steps) and the engine's fade, for every colour */
    unsigned short cur = activePalette[skyIdx];
    int dr = (int)(cur >> 11) - (int)(sky565 >> 11), dg = (int)((cur >> 6) & 31) - (int)((sky565 >> 6) & 31),
        db = (int)(cur & 31) - (int)(sky565 & 31);
    int fa = fadeMode > 0 ? (fadeA > 255 ? 255 : fadeA) : 0;
    int fr = fadeR >> 3, fg = fadeG >> 3, fb = fadeB >> 3;
    if (stageMode == STAGEMODE_PAUSED) /* the pause menu: dimmed as the planes (pause_frame) */
        fa = 150, fr = fg = fb = 0;
    bool adjust = dr || dg || db || fa > 0;
    int fk      = 255 - fa;
    /* the colours of the frame before, again (no vertical move, the same sky colour and fade): nothing to write - most
       frames of most acts (2-3% of a frame each time: 224 colours from the cart's table to the 32X) */
    unsigned int row = (unsigned int)(skyY >> 1) | (unsigned int)cur << 16;
    unsigned int fad = 0x80000000u | (unsigned int)fa | (unsigned int)fr << 8 | (unsigned int)fg << 13 | (unsigned int)fb << 18;
    if (skyShown && row == skyLastRow && fad == skyLastFade)
        return;
    skyLastRow  = row;
    skyLastFade = fad;
    while (!(SC_VDP_FBCTL & MARS_VDP_PEN)) {
    } /* the palette can be written (blank) */
    volatile unsigned short *cram = SC_CRAM + SKY_BASE;
    const unsigned int *pairs     = (const unsigned int *)skyPairs; /* A and B in one read (the table is 4-aligned) */
    int k                         = skyY >> 1;
    for (int g = 0; g < 112; ++g, ++k) {
        if (k >= skyPairsN)
            k -= skyPairsN;
        unsigned int ab  = pairs[k];
        unsigned short A = (unsigned short)(ab >> 16), B = (unsigned short)ab;
        if (adjust)
            for (int j = 0; j < 2; ++j) {
                unsigned short c = j ? B : A;
                int r = (c & 31) + dr, gg = ((c >> 5) & 31) + dg, b = ((c >> 10) & 31) + db;
                r  = r < 0 ? 0 : r > 31 ? 31 : r;
                gg = gg < 0 ? 0 : gg > 31 ? 31 : gg;
                b  = b < 0 ? 0 : b > 31 ? 31 : b;
                if (fa > 0) {
                    r  = sky_div255(r * fk + fr * fa);
                    gg = sky_div255(gg * fk + fg * fa);
                    b  = sky_div255(b * fk + fb * fa);
                }
                c = (unsigned short)(b << 10 | gg << 5 | r);
                if (!c)
                    c = 0x0400; /* never 0x0000: a see-through 32X pixel */
                if (j)
                    B = c;
                else
                    A = c;
            }
        cram[2 * g]     = A;
        cram[2 * g + 1] = B;
    }
    if (!skyShown) { /* the first frame of the act with the picture: the 32X layer on (256 colours, priority bit clear) */
        MARS_VDP_DISPMODE = (unsigned short)((MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256);
        skyShown          = true;
    }
}
// ---------------------------------------------------------------- REV R4B: the loading picture
// At power-on the boot loader shows IMAGE.RAW (tools/scd_splash.py: the title screen with a loading bar) while it loads,
// the Genesis display off (cd32x/sh2_engboot.c). Between the scenes the engine does the same: the picture from its copy
// at the end of SCDDATA.PAK in the cart (tools/build_game_data.py: the footer 'SPL1', its offset, its size) into the
// frame buffer the 32X shows (swap, write, swap back - as the sky's picture: the engine's data stays in the other
// buffer), the Genesis display off (Sub-CPU command 52) - its loading lines are not seen - and the bar filled chunk by
// chunk: its segments are colours of their own, set to the full colour (nothing is drawn). The colours have bit 15: in
// front of the Genesis picture. The act's planes (command 68 op 1) switch the Genesis display on again, and the act's
// sky or a blank 32X layer replaces the picture. A reload of the scene already in the cart (a restart after a death)
// reads nothing from the disc: the screen just stays black. Something not loading brings the text back for its STATE.
#define SPLASH_PIXELS (320 * 224)
#define SPLASH_BYTES (SPLASH_PIXELS + 512 + 12) /* the pixels, 256 colours, 'BAR1' + 4 words */
static void sub_cmd(unsigned short cmd, unsigned short arg);
static bool splashUp = false; /* the 32X layer shows the loading picture */
static int splashFrom = 0, splashTo = 0, splashLit = 0, splashSegs = 0;
static unsigned short splashFirst = 0, splashFull = 0;
static unsigned int be32(const unsigned char *p) { return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3]; }
static const unsigned char *splash_image() /* the picture after the data pack (cart 0x300000), or 0 */
{
    unsigned int n = SCD_BOOT_RECORD[2]; /* SCDDATA.PAK's length */
    if (n < SPLASH_BYTES + 16 || n > 0x80000)
        return 0;
    const unsigned char *t = CART_PACK + n - 16;
    unsigned int off       = be32(t + 4);
    if (be32(t) != 0x53504C31u || be32(t + 8) != SPLASH_BYTES || (off & 3) || off + SPLASH_BYTES > n - 16) /* 'SPL1' */
        return 0;
    if (be32(CART_PACK + off + SPLASH_PIXELS + 512) != 0x42415231u) /* 'BAR1' */
        return 0;
    return CART_PACK + off;
}
static void splash_show() __attribute__((noinline));
static void splash_show()
{
    const unsigned char *img = splash_image();
    if (!img)
        return; /* SCDDATA.PAK without the picture: the text stays, as before */
    const unsigned char *bar = img + SPLASH_PIXELS + 512;
    splashFirst              = (unsigned short)(bar[4] << 8 | bar[5]);
    splashSegs               = bar[6] << 8 | bar[7];
    splashFull               = (unsigned short)(bar[10] << 8 | bar[11]);
    if (splashSegs <= 0 || splashFirst + splashSegs > 256)
        return;
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* blank while the engine's buffer is shown */
    flip_wait();
    volatile unsigned short *fb = SC_FB;
    for (int y = 0; y < 256; ++y) fb[y] = (unsigned short)(0x100 + (y < 224 ? y : 223) * 160);
    const unsigned int *src       = (const unsigned int *)img;
    volatile unsigned short *line = fb + 0x100;
    for (int i = 0; i < SPLASH_PIXELS / 4; ++i) { /* 16-bit writes (Kobo rule 4) */
        unsigned int w  = src[i];
        line[2 * i]     = (unsigned short)(w >> 16);
        line[2 * i + 1] = (unsigned short)w;
    }
    flip_wait(); /* the picture is the shown buffer, the engine's buffer the SH2's again */
    skyPicture                = false;
    const unsigned short *pal = (const unsigned short *)(img + SPLASH_PIXELS);
    while ((SC_VDP_FBCTL & 0x8000) == 0) { /* VBLK: the palette in the vertical blank */
    }
    for (int i = 0; i < 256; ++i) SC_CRAM[i] = pal[i];
    MARS_VDP_DISPMODE = (unsigned short)((MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256);
    splashUp          = true;
    splashLit         = 0;
    splashFrom        = 0;
    splashTo          = splashSegs;
}
static void splash_part(int from, int to) /* the bar's share of the next file: segments from - to of 32 */
{
    splashFrom = from * splashSegs >> 5;
    splashTo   = to * splashSegs >> 5;
}
static void splash_chunk(unsigned int done, unsigned int total) __attribute__((noinline)); /* cart code: between chunks */
static void splash_chunk(unsigned int done, unsigned int total) /* chunk `done` of `total` loaded: the bar up to there */
{
    if (!splashUp || !total)
        return;
    int n = splashFrom + (int)((unsigned int)(splashTo - splashFrom) * done / total);
    if (n <= splashLit)
        return;
    while ((SC_VDP_FBCTL & 0x8000) == 0) { /* VBLK */
    }
    for (; splashLit < n && splashLit < splashSegs; ++splashLit) SC_CRAM[splashFirst + splashLit] = splashFull;
}
/* REV R5C: a line of text under the loading picture's bar (the owner: "let the user know that the next level needs to load
   everything for the next level to the cart") - Kobo's 3 x 5 font, each pixel 2 x 2, the picture's lightest colour on its
   darkest. Into the shown buffer: the display blank while the buffers are swapped for it (two frames black) */
static void splash_note(const char *t, const char *above = 0) __attribute__((noinline));
static void splash_note(const char *t, const char *above)
{
    const unsigned char *img = splash_image();
    if (!splashUp || !img)
        return;
    const unsigned short *pal = (const unsigned short *)(img + SPLASH_PIXELS);
    int hi = 1, lo = 1, hv = -1, lv = 1 << 30;
    for (int i = 1; i < 256; ++i) { /* (not the bar's colours: they change) */
        if (i >= splashFirst && i < splashFirst + splashSegs)
            continue;
        const unsigned int c = pal[i];
        const int v = (int)((c & 31) + (c >> 5 & 31) + (c >> 10 & 31));
        if (v > hv)
            hv = v, hi = i;
        if (v < lv)
            lv = v, lo = i;
    }
    const unsigned short d = MARS_VDP_DISPMODE;
    MARS_VDP_DISPMODE = (unsigned short)(d & 0x8000);
    flip_wait(); /* (the SH2 now writes the picture's buffer) */
    volatile unsigned short *line = SC_FB + 0x100;
    for (int l = 0; l < 2; ++l) { /* below the bar (rows 201-208 of IMAGE.RAW), then the line above it (REV R5D) */
        const char *u = l ? above : t;
        if (!u)
            continue;
        int n = 0;
        while (u[n]) ++n;
        const int x0 = (320 - n * 8) / 2 & ~1, y0 = l ? 188 : 212;
        for (int y = y0 - 2; y < y0 + 12; ++y)
            for (int x = x0 - 4; x < x0 + n * 8 + 4; x += 2) line[y * 160 + x / 2] = (unsigned short)(lo << 8 | lo);
        for (int k = 0; k < n; ++k) {
            const unsigned char *g = font3x5[glyph(u[k])];
            for (int r = 0; r < 10; ++r)
                for (int c = 0; c < 3; ++c)
                    if (g[r / 2] & (4 >> c))
                        line[(y0 + r) * 160 + (x0 + k * 8 + 2 * c) / 2] = (unsigned short)(hi << 8 | hi);
        }
    }
    flip_wait(); /* the picture shown again */
    MARS_VDP_DISPMODE = d;
}
static void splash_fail() /* a file did not load: the picture off, the Genesis display on - line 10 shows the STATE */
{
    splashUp          = false;
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000);
    sub_cmd(52, 1);
    genesisOff = false;
}
enum { PLANES_OFF_TEXT, PLANES_OFF_PICTURE, PLANES_OFF_BLACK }; /* REV R4B: what shows while the next scene loads */
static void big_home_w1(); /* REV R4G (THE PRELOAD, below) */
static void planes_off(int show = PLANES_OFF_PICTURE)
{
    big_home_w1(); /* REV R4G: window 1 shows page 1 again - the loading picture, the menu's font slot */
    sky_off();     /* REV R4B */
    aniTiles = 0; /* REV R4C: the act file's slot may get another file (the menu's, the credits'): planes_load sets it */
    if (!planesOn && !menuOn)
        return;
    if (show == PLANES_OFF_PICTURE)
        splash_show(); /* REV R4B: in front of the planes, before they go */
    planesOn = false;
    menuOn   = false;
    sgb_cmd(2, 0); /* the listener's own picture back (text, for the loading lines) */
    SC_COMM4 = 0;  /* REV R4: the last record id - the Sub-CPU took it for a start-up fault ("SH2 NEVER EVEN REACHED") */
    if (splashUp || show == PLANES_OFF_BLACK) {
        sub_cmd(52, 0); /* REV R4B: the Genesis display off - its loading lines are not seen */
        genesisOff = true;
    }
}
/* REV R3: Tails. A scene played as Tails (the game's character choice: playerListPos 1) has files of its own,
   numbered 128 on: its scene image L<list>S<pos + 128>.IMG (the cart image holds the scene's animation files and
   sprite sheets in the order the scripts load them - Tails's) and its act file G<list>S<pos + 128>.MDG (its sprite
   pack has Tails's frames). tools/build_game_data.py and tools/build_mdg.sh make them ("1:0@1"). */
static unsigned int player_files()
{
    /* REV R4C: the presentation scenes (the title, the menus ...) are the same for both: Sonic's files */
    return (playerListPos == 1 && activeStageList != STAGELIST_PRESENTATION && stageListPosition >= 0 && stageListPosition < 0x80) ? 0x80u : 0u;
}
static unsigned int mdgInCart = 0; /* REV R4B: the act file in the cart's slot (0: none, or another file) - REV R4G: the
                                     slot window 1 shows (sgbOff) */
static unsigned int mdg_file() { return 0x1000 + (((unsigned int)activeStageList + 1) << 8) + (unsigned int)stageListPosition + player_files(); }
static int sd_load_whole(unsigned short number, unsigned int *bytes, unsigned int at = 0x380000u);
static unsigned char barImgTo = 20, barMdgFrom = 20; /* REV R4G: the loading bar's shares (32 segments): the scene's
                                                        image 0 - barImgTo, its act file barMdgFrom - 32 (a preload: the
                                                        rest between) */
static void planes_load() /* the scene's MDG: on the disc? then into the cart and the VDP */
{
    unsigned int file  = mdg_file();
    unsigned int bytes = 0;
    int bad            = 0;
    STATE(0x5C60); /* R1C: the MDG load starts */
    comm_idle();   /* REV R4: the pad request answered first */
    if (file == mdgInCart && *(const unsigned int *)SGB_FILE == SGB_MAGIC)
        bytes = *(const unsigned int *)(SGB_FILE + 8); /* REV R4B: the same act again (a restart): its file is in the cart
                                                          - REV R4G: or preloaded (its own length, in its header) */
    else {
        mdgInCart = 0;
        splash_part(barMdgFrom, 32); /* REV R4B: the bar's last part */
        bad = sd_load_whole((unsigned short)file, &bytes, sgbOff);
        if (bad < 0) {
            STATE(0x5C61);
            splash_fail();
            return;
        }
        *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    }
    const unsigned char *f = SGB_FILE;
    if (bad > 0 || *(const unsigned int *)f != SGB_MAGIC || *(const unsigned short *)(f + 4) != SGB_VERSION ||
        *(const unsigned int *)(f + 8) != bytes) {
        STATE(0x5C62);
        splash_fail();
        return;
    }
    mdgInCart = file;
    aniTiles  = 0; /* REV R4C: 'ANI1' - the file's last 8 bytes (the section's offset, the tag) */
    if (!(bytes & 3) && bytes >= 8 + 192 && *(const unsigned int *)(f + bytes - 4) == SGB_ANI_TAG) {
        unsigned int at = *(const unsigned int *)(f + bytes - 8);
        if (at <= bytes - 8 - 192)
            aniTiles = f + at;
    }
    if (aniTiles) /* a tile that shows a picture without name words (copied while no act file was in): its own again */
        for (int t = 0; t < TILE_COUNT; ++t) {
            unsigned int s = pmTileMap[t];
            if (!(aniTiles[s >> 3] >> (s & 7) & 1))
                pmTileMap[t] = (unsigned short)t;
        }
    for (int i = 0; i < 32; ++i) aniDirty[i] = 0;
    aniDirtyAny = false;
    STATE(0x5C65); /* R1C: the MDG is in the cart; command 68 next */
    SC_COMM4 = 0; /* R1S (R1P's line): COMM4 still held the Sub-CPU's start-up check 0x1234 - the 68000 took it as a
                     record id the moment command 68 switched the records on and copied junk from the frame buffer
                     (R1H: ID 1234, REC 0004 0004 0006 ...): R1G's and R1R's dot in every blank cell. Cleared here by
                     the SH2, before command 68. */
    if (sgb_cmd(1, SGB_CART_OFFSET) != 1) {
        STATE(0x5C63);
        splash_fail();
        return;
    }
    const unsigned short *cram0 = (const unsigned short *)(f + *(const unsigned int *)(f + 32));
    for (int i = 0; i < 64; ++i) cramSent[i] = cram0[i];
    for (int i = 0; i < 64; ++i) waterSent[i] = 0xFFFF; /* REV R4D */
    cramSync = scdWaterSwitch != 0;
    wN       = 0;
    /* REV R3: the sprite pack after the plane data ('SPRP' + offset in the header's last 8 bytes); its cells go
       after the act's plane cells, up to plane A's name table (0xC000) */
    mds_init(&sprites, *(const unsigned int *)(f + 56) == MDS_TAG ? f + *(const unsigned int *)(f + 60) : 0,
             *(const unsigned short *)(f + 6), 0xC000 / 32);
    md_plane_reset(&sgbA);
    md_plane_reset(&sgbB);
    sgbBLayer = -1;
    for (int s = 0; s < 4; ++s) slotUsed[s] = false;
    frameBg     = -1;
    layoutQn    = 0; /* REV R4B */
    layoutQLost = false;
    planesOn    = true;
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* 32X layer blank: the Genesis shows */
    splashUp          = false;                                         /* REV R4B: the loading picture off */
    sky_load(f); /* REV R4B: the act's 32X sky (on from its first frame with a picture) */
}
static bool sgb_shows(int slots, bool front)
{
    for (int s = 0; s < 4; ++s)
        if ((slots >> s & 1) && (s >= tLayerMidPoint) == front)
            return true;
    return false;
}
/* the frame's record (built in the frame buffer) to the 68000: give_fb, COMM4 = id; the 68000 copies it and answers
   COMM6 = id; take_fb. REV R4: shared with the dev menu (inlined into both) */
static inline __attribute__((always_inline)) void rec_handover()
{
    recId = (unsigned short)(recId + 1);
    if (!recId)
        recId = 1;
    sgb_give_fb();
    SC_COMM4 = recId;
    unsigned int n = 0;
    for (; n < 400000u; ++n) { /* the same answer twice: a read during the 68000's write is undefined */
        if (SC_COMM6 == recId && SC_COMM6 == recId)
            break;
        sgb_pause();
    }
    sgb_take_fb();
    if (n >= 400000u) {
        planes_off(PLANES_OFF_TEXT); /* (REV R4B: the text, for line 10) */
        STATE(0x5C64);
    }
}
/* REV R4B: the pause menu. While the game is paused the engine draws no tile layer (the planes keep the last picture)
   but every draw list again: the pause menu's own sprites (CONTINUE, RESTART, EXIT, the PAUSE banner, the player's
   icon) go to the 68000 as a record of sprites only, with the planes' colours (lines 0-2) dimmed so the menu reads over
   them - the engine's version washes the picture out and draws a black panel (DrawRect), which the Genesis has no use
   for. Cart code (not every frame); the scroll the last frame sent. */
static uint16_t lastVA = 0, lastVB = 0;
static void pause_frame() __attribute__((noinline));
static void pause_frame()
{
    const unsigned char *f      = SGB_FILE;
    const unsigned short *slots = (const unsigned short *)(f + *(const unsigned int *)(f + 16));
    SgbOut o;
    sgb_out_begin(&o, SGB_FB_REC, SGB_REC_MAX);
    mds_record(&sprites, &o);
    uint16_t cIdx[64], cCol[64];
    int nc = 0;
    /* REV R4D: the UNDERWATER switch on - the colours of line 0's bank above the water line (the engine leaves
       activePalette at the last line's bank: R4C showed the water's colours on the whole screen) */
    const unsigned short *pal = scdWaterSwitch ? fullPalette[gfxLineBuffer[0]] : activePalette;
    for (int i = 0; i < 64; ++i) {
        if (slots[i] == 0xFFFF) {
            if (cramSync) /* REV R4D: the act's first record - all 64 (the 68000's shadow) */
                cIdx[nc] = (uint16_t)i, cCol[nc++] = cramSent[i];
            continue;
        }
        uint16_t c565 = pal[slots[i]];
        if (i < 48)
            c565 = sgb_fade565(c565, 0, 0, 0, 150); /* the planes at about 40 % */
        uint16_t c = sgb_md_color(c565);
        if (c != cramSent[i] || cramSync) {
            cIdx[nc]    = (uint16_t)i;
            cCol[nc++]  = c;
            cramSent[i] = c;
        }
    }
    int n = sgb_out_end(&o, lastVA, lastVB, hsA, hsB, SCREEN_YSIZE, cIdx, cCol, nc);
    if (n >= 0 && scdWaterSwitch)
        n = water_out(&o, slots, true); /* REV R4D */
    if (n < 0) {
        colours_lost(slots, cIdx, nc);
        mds_forget(&sprites);
        return;
    }
    cramSync = false;
    wN       = 0;
    rec_handover();
}
static void planes_frame() SCD_HOT; /* PORT32X R1V: every frame - SDRAM (RetroEngine.hpp SCD_HOT) */
static void planes_frame() /* the frame's record -> the 68000 (as host/md_check.cpp sgb_frame) */
{
    int fgSlots = 0, bgLayer = -1, bgSlots = 0;
    bool unsupported = false;
    for (int s = 0; s < 4; ++s) {
        if (activeTileLayers[s] >= LAYER_COUNT)
            continue;
        if (stageLayouts[activeTileLayers[s]].type != LAYER_HSCROLL || !slotUsed[s]) {
            unsupported = true;
            continue;
        }
        if (slotLayer[s] == 0)
            fgSlots |= 1 << s;
        else if (bgLayer < 0 || bgLayer == slotLayer[s]) {
            bgLayer = slotLayer[s];
            bgSlots |= 1 << s;
        }
        else
            unsupported = true;
    }
    for (int s = 0; s < 4; ++s) slotUsed[s] = false;
    int bgSeen = frameBg;
    frameBg    = -1;
    if (unsupported || !fgSlots || bgLayer != bgSeen) {
        if (!fgSlots && stageMode == STAGEMODE_PAUSED && sprites.ndraw) { /* REV R4B: the pause menu */
            pause_frame();
            sprites.ndraw = 0;
            return;
        }
        sprites.ndraw = 0; /* REV R3: the sprites keep the last picture too */
        return;            /* the planes keep the last picture */
    }
    const unsigned char *f = SGB_FILE;
    MdLayer LA, LB;
    LA.chunks       = (const uint16_t *)(f + *(const unsigned int *)(f + 24));
    LA.quads        = (const uint16_t *)(f + *(const unsigned int *)(f + 20));
    LA.tileMap      = aniTiles ? pmTileMap : 0; /* REV R4C: the act's animated tiles (Copy16x16Tile) */
    LB              = LA;
    LA.layout       = stageLayouts[0].tiles;
    LA.layoutStride = 0x100;
    LA.wChunks      = stageLayouts[0].xsize;
    LA.hChunks      = stageLayouts[0].ysize;
    LA.showBack     = sgb_shows(fgSlots, false);
    LA.showFront    = sgb_shows(fgSlots, true);
    SgbOut o;
    sgb_out_begin(&o, SGB_FB_REC, SGB_REC_MAX);
    mds_record(&sprites, &o); /* REV R3: the patterns and the sprite table first - vertical blank */
    uint16_t vA = 0, vB = 0;
    bool freshA = false, freshB = false; /* REV R4C: a plane written from scratch this frame (no refresh needed) */
    if (layoutQLost) { /* REV R4B */
        md_plane_reset(&sgbA);
        md_plane_reset(&sgbB);
        layoutQn    = 0;
        layoutQLost = false;
        freshA = freshB = true;
    }
    md_plane_update(&sgbA, &LA, SGB_NAME_A, tileYA, xA, SCREEN_YSIZE, SCREEN_XSIZE, hsA, &vA, sgb_write, &o);
    skyY = bgLayer >= 0 && bgLayer == skyLayer ? tileYB : -1; /* REV R4B: the sky follows the background's rows */
    if (bgLayer >= 0) {
        if (bgLayer != sgbBLayer) {
            md_plane_reset(&sgbB);
            sgbBLayer = bgLayer;
            freshB    = true;
        }
        LB.layout       = stageLayouts[bgLayer].tiles;
        LB.layoutStride = 0x100;
        LB.wChunks      = stageLayouts[bgLayer].xsize;
        LB.hChunks      = stageLayouts[bgLayer].ysize;
        LB.showBack     = sgb_shows(bgSlots, false);
        LB.showFront    = sgb_shows(bgSlots, true);
        md_plane_update(&sgbB, &LB, SGB_NAME_B, tileYB, xB, SCREEN_YSIZE, SCREEN_XSIZE, hsB, &vB, sgb_write, &o);
    }
    else
        for (int i = 0; i < SCREEN_YSIZE; ++i) hsB[i] = 0;
    for (int i = 0; i < layoutQn; ++i) /* REV R4B: the changed entries' cells the planes show */
        if (layoutQ[i][0] == 0)
            md_plane_chunk(&sgbA, &LA, SGB_NAME_A, layoutQ[i][1], layoutQ[i][2], sgb_write, &o);
        else if (layoutQ[i][0] == bgLayer)
            md_plane_chunk(&sgbB, &LB, SGB_NAME_B, layoutQ[i][1], layoutQ[i][2], sgb_write, &o);
    layoutQn = 0;
    if (aniDirtyAny) { /* REV R4C: the changed animated tiles' cells the planes hold (a plane written from scratch this
                          frame has them already - the record stays small) */
        if (!freshA)
            md_plane_refresh(&sgbA, &LA, SGB_NAME_A, aniTiles + 128, (const uint8_t *)aniDirty, sgb_write, &o);
        if (bgLayer >= 0 && !freshB)
            md_plane_refresh(&sgbB, &LB, SGB_NAME_B, aniTiles + 128, (const uint8_t *)aniDirty, sgb_write, &o);
        for (int i = 0; i < 32; ++i) aniDirty[i] = 0;
        aniDirtyAny = false;
    }
    const unsigned short *slots = (const unsigned short *)(f + *(const unsigned int *)(f + 16));
    uint16_t cIdx[64], cCol[64];
    int nc = 0;
    /* REV R4D: the UNDERWATER switch on - line 0's bank above the water line (activePalette: the last line's) */
    const unsigned short *pal = scdWaterSwitch ? fullPalette[gfxLineBuffer[0]] : activePalette;
    for (int i = 0; i < 64; ++i) {
        if (slots[i] == 0xFFFF) {
            if (cramSync) /* REV R4D: the act's first record - all 64 (the 68000's shadow) */
                cIdx[nc] = (uint16_t)i, cCol[nc++] = cramSent[i];
            continue;
        }
        uint16_t c565 = pal[slots[i]]; /* REV R4B: the active bank (Palmtree Panic's waterfalls cycle the banks) */
        if (fadeMode > 0) /* REV R3: the engine's fade rectangle, on the palette - planes and sprites fade with it */
            c565 = sgb_fade565(c565, fadeR, fadeG, fadeB, fadeA);
        uint16_t c = sgb_md_color(c565);
        if (c != cramSent[i] || cramSync) {
            cIdx[nc]    = (uint16_t)i;
            cCol[nc++]  = c;
            cramSent[i] = c;
        }
    }
    int recWords = sgb_out_end(&o, vA, vB, hsA, hsB, SCREEN_YSIZE, cIdx, cCol, nc);
    if (recWords >= 0 && scdWaterSwitch)
        recWords = water_out(&o, slots, false); /* REV R4D: the water line */
    if (recWords < 0) {
        colours_lost(slots, cIdx, nc); /* REV R4D */
        md_plane_reset(&sgbA); /* too big: both planes from scratch next frame */
        md_plane_reset(&sgbB);
        mds_forget(&sprites); /* REV R3: its pattern uploads never reached the VRAM */
        return;
    }
    lastVA = vA, lastVB = vB; /* REV R4B: the pause menu's records keep this scroll */
    cramSync = false;
    wN       = 0;
    rec_handover();
}
// ---------------------------------------------------------------- REV R4: the dev menu on plane A
// The engine's dev menu (engine/Debug.cpp) draws its text entries every frame (plat_menu_text). The SH2 keeps the
// frame's text as plane A name words (40 x 28) and the words plane A shows, both in the frame buffer's record area after
// the record (a menu record is small: at most a word per character), and sends what changed as a plane record
// (md/sgb.h), the act files' way. The font: G0S255.MDG (tools/build_game_data.py - the game's own 8 x 8 font,
// SystemText.gif, as Genesis cells: character c is cell 1 + c, highlighted 257 + c), loaded into the act file slot
// and set up by command 68 as an act file. Status codes: 5C70 font load, 5C71 not on the disc, 5C72 its header bad,
// 5C73 command 68 refused it, 5C74 the menu shows.
#define MENU_FILE 0x11FF /* G0S255.MDG */
#define MENU_COLS 40
#define MENU_ROWS 28
#define MENU_REC_MAX 3072 /* words: the record, before the two text copies */
#define MENU_CUR ((volatile unsigned short *)0x24001800)
#define MENU_SHOWN ((volatile unsigned short *)(0x24001800 + 2 * MENU_COLS * MENU_ROWS))
static bool menuDrawn = false, menuFailed = false;
static void pm_end(bool picture);
void plat_menu_clear()
{
    if (Engine.gameMode != ENGINE_DEVMENU)
        return;
    pm_end(false); /* REV R4C: from a scene in the 32X picture mode (A + B + C + START): its text is the menu's again */
    for (int i = 0; i < MENU_COLS * MENU_ROWS; ++i) MENU_CUR[i] = 0;
    menuDrawn = true;
}
void plat_menu_text(const unsigned short *t, int n, int x, int y, int highlight)
{
    if (!menuDrawn || y < 0 || (y >> 3) >= MENU_ROWS)
        return;
    volatile unsigned short *row = MENU_CUR + (y >> 3) * MENU_COLS;
    int col                      = (x + 4) >> 3; /* the menus' half-cell positions (centred, right-aligned): the nearest cell */
    for (int i = 0; i < n; ++i, ++col) {
        unsigned int c = t[i] & 0xFF;
        if (col >= 0 && col < MENU_COLS)
            row[col] = (unsigned short)(c == ' ' ? 0 : 1 + c + (highlight ? 256 : 0));
    }
}
static bool menu_on()
{
    if (menuFailed) {
        if (genesisOff)
            splash_fail(); /* REV R4B: the text back once (line 10: why the menu did not come) */
        return false;
    }
    planes_off(); /* a stage's planes (the A + B + C + START way back) */
    unsigned int bytes = 0;
    STATE(0x5C70);
    comm_idle();
    mdgInCart = 0; /* REV R4B: the menu's file takes the act file's cart slot */
    int bad = ((ScdLoadFile)SCD_BOOT_RECORD[6])((unsigned short)MENU_FILE, &bytes);
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    const unsigned char *f                = SGB_FILE;
    if (bad != 0 || *(const unsigned int *)f != SGB_MAGIC || *(const unsigned short *)(f + 4) != SGB_VERSION ||
        *(const unsigned int *)(f + 8) != bytes) {
        STATE(bad < 0 ? 0x5C71 : 0x5C72);
        menuFailed = true; /* not again every frame: each try reads the disc */
        splash_fail();     /* REV R4B */
        return false;
    }
    SC_COMM4 = 0;
    if (sgb_cmd(1, SGB_CART_OFFSET) != 1) {
        STATE(0x5C73);
        menuFailed = true;
        splash_fail(); /* REV R4B */
        return false;
    }
    for (int i = 0; i < MENU_COLS * MENU_ROWS; ++i) MENU_SHOWN[i] = 0; /* command 68 cleared the planes */
    for (int i = 0; i < SCREEN_YSIZE; ++i) hsA[i] = hsB[i] = 0;
    menuOn            = true;
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* 32X layer blank: the Genesis shows */
    splashUp          = false;                                         /* REV R4B: the loading picture off */
    STATE(0x5C74);
    return true;
}
static void menu_frame()
{
    menuDrawn = false;
    if (!menuOn && !menu_on())
        return;
    SgbOut o;
    sgb_out_begin(&o, SGB_FB_REC, MENU_REC_MAX);
    for (int r = 0; r < MENU_ROWS; ++r)
        for (int c = 0; c < MENU_COLS; ++c) {
            int i            = r * MENU_COLS + c;
            unsigned short w = MENU_CUR[i];
            if (w != MENU_SHOWN[i]) {
                MENU_SHOWN[i] = w;
                sgb_write(&o, (uint16_t)(SGB_NAME_A + 2 * (r * 64 + c)), w);
            }
        }
    if (sgb_out_end(&o, 0, 0, hsA, hsB, SCREEN_YSIZE, 0, 0, 0) < 0) {
        for (int i = 0; i < MENU_COLS * MENU_ROWS; ++i) MENU_SHOWN[i] = 0xFFFF; /* not sent: all of it next frame */
        return;
    }
    rec_handover();
}
void scd_text_on() { splash_fail(); } /* REV R4B: for the fatal screens (engine_start.cpp's exception screen, no_heap) */
// ---------------------------------------------------------------- REV R4C: the 32X picture mode (cd32x/engine/scd_pm.h)
// The scenes whose image carries the sprite sheets' and the tiles' pixels (tools/build_game_data.py: the presentation
// and the special stage lists - the title, the menus, time attack, the special stages ...) are drawn by the SH2 into the
// 32X frame buffer in 256 colours, the Genesis display off. The two frame buffers take turns: the SH2 draws a frame into
// one while the 32X shows the other; they swap in the vertical blank. Each holds the line table, the picture
// (PM_FB_PIXELS: 224 lines of 328), the 3D scene's vertices, the scene's layouts (PM_FB_POOL - the layout pool of these
// scenes: the engine reads its layouts from whichever buffer the SH2 has, so both hold the same - copied when the scene
// starts, a script's change written into the other buffer too, after the swap) and the engine's one-line buffer
// (SCD_FB_LINE). The colours are the engine's palette with its fade, written in each vertical blank after a swap. A
// frame that takes longer than its vertical blank makes the next ones logic only (pmSkip), up to PM_SKIP_MAX in a row,
// until the game is back on time: the game keeps its speed, the picture comes less often.
// Status codes: 5C90 the picture mode starts (both buffers set up), 5C91 done.
bool scdPicture = false;
static bool pmScene = false; /* the scene in the cart is drawn in the picture mode (plat_stage_loading) */
static bool pmReady = false; /* both frame buffers set up for it (pm_begin) */
static bool pmShown = false; /* the 32X shows its picture */
static bool pmDrew  = false; /* a draw call since the last frame shown */
static const unsigned char *pmGfx = 0, *pmTiles = 0;
static int pmTileY[4];
static unsigned int pmVbl0 = 0, pmLogic = 0;
static int pmSkipRun = 0;
#define PM_SKIP_MAX 10
#define PM_FB16 ((volatile unsigned short *)(0x24000000 + PM_FB_PIXELS))
#define PM_OV16 ((volatile unsigned short *)(0x24020000 + PM_FB_PIXELS)) /* the overwrite image: 0 bytes not written */
#define PM_W16(off, w) (PM_FB16[(off) >> 1] = (unsigned short)(w))
#define PM_O16(off, w) (PM_OV16[(off) >> 1] = (unsigned short)(w))
#define PM_O8(off, v) (PM_OV16[(off) >> 1] = ((off) & 1) ? (unsigned short)(unsigned char)(v) : (unsigned short)((unsigned char)(v) << 8))
#define PM_GFX pmGfx
#define PM_TILES pmTiles
/* the chunk table's arrays in the cart, read directly (the proxy, tiles128x128, only while a script has patched an
   entry: Set16x16TileInfo - none of these scenes does) */
static inline int pm_chunk_tile(int e)
{
    if (scdChunkPatchCount)
        return tiles128x128.gfxDataPos[e] >> 8;
    return (unsigned)e < (unsigned)scdChunkCount ? scdChunkIndex[e] : 0;
}
static inline int pm_chunk_dir(int e)
{
    if (scdChunkPatchCount)
        return tiles128x128.direction[e];
    return (unsigned)e < (unsigned)scdChunkCount ? scdChunkDir[e] : 0;
}
static inline int pm_chunk_plane(int e)
{
    if (scdChunkPatchCount)
        return tiles128x128.visualPlane[e];
    return (unsigned)e < (unsigned)scdChunkCount ? scdChunkPlane[e] : 0;
}
#define PM_CHUNK_TILE(e) pm_chunk_tile(e)
#define PM_CHUNK_DIR(e) pm_chunk_dir(e)
#define PM_CHUNK_PLANE(e) pm_chunk_plane(e)
/* the SH2's on-chip divider (32 / 32 bits, signed, 39 cycles): the library's __sdivsi3 is a loop. No interrupt handler
   of this program divides (the loader's vertical blank counts and samples only) */
static inline int pm_div(int a, int b)
{
    *(volatile int *)0xFFFFFF00 = b; /* DVSR */
    *(volatile int *)0xFFFFFF04 = a; /* DVDNT: starts the division */
    return *(volatile int *)0xFFFFFF04;
}
#define PM_DIV(a, b) pm_div((a), (b))
#define PM_BE16(p) (*(const unsigned short *)(p)) /* (big-endian: one read) */
#define PM_SKY_ROWS2 1 /* the special stages' floor: every other line sampled, shown twice (half its cost) */
static bool floorHW = false; /* REV R5: the special stage's floor on the Sega CD's graphics chip (below) */
static bool flrTitle = false;           /* REV R5E: ... the title's clouds instead (FLOOR9.BIN) */
static unsigned int ttlSky = 0x000060u; /* REV R5E: the title's sky (its full-screen rectangle): the Genesis backdrop */
static bool title_hw_frame(int v32);    /* REV R5E (below) */
/* REV R5B: the floor's first row (0 - 100 from line 124) that lies inside the floor's map from its first pixel to its
   last - from there on, the chip's floor. The rows above it (the far ones, at the horizon) reach past the map's edge,
   where the engine's floor is see-through and shows the sky behind it: those rows the SH2 still draws itself, over the
   sky, as R4N did (pm_sky_rows), and the chip's picture of them stays hidden. The chip's arithmetic, on the values it
   gets (cd32x/sub_main.c floor_trace: sample 1 of the row below, 12 bits of fraction) */
static int flr_first_row(const TileLayer *layer)
{
    const int lw = (int)layer->xsize << 19, lh = (int)layer->ysize << 19; /* (the map's size, 12 bits of fraction) */
    int fx = layer->XPos >> 13, fz = layer->ZPos >> 13; /* (as sent: 13.3) */
    fx      = fx < -32768 ? -32768 : fx > 32767 ? 32767 : fx;
    fz      = fz < -32768 ? -32768 : fz > 32767 ? 32767 : fz;
    const int x = fx << 9, z = fz << 9, y = layer->YPos;
    const int sn = sinMLookupTable[layer->angle & 0x1FF], cs = cosMLookupTable[layer->angle & 0x1FF];
    for (int k = 0; k < 100; ++k) {
        int q  = pm_div(y, (9 + 2 * k) << 8);
        int xb = (q * -cs) >> 9, yb = (sn * q) >> 9;
        int X = x + ((3 * sn * q) >> 2) - xb * 320 + xb, Y = z + ((3 * cs * q) >> 2) - yb * 320 + yb;
        int X2 = X + 2 * 319 * xb, Y2 = Y + 2 * 319 * yb;
        if (!(xb > 32767 || xb < -32768 || yb > 32767 || yb < -32768) && X >= 0 && X < lw && Y >= 0 && Y < lh && X2 >= 0 &&
            X2 < lw && Y2 >= 0 && Y2 < lh)
            return k; /* (the map is a rectangle: both ends inside, the whole row inside) */
    }
    return 100;
}
/* REV R5B: with the chip's floor, the line-scroll layers (the sky, the horizon) only down to the floor's first row: the
   rows below are colour 0 in the 32X picture (floor_hw_frame), so what the layers drew there was never seen */
static int pm_hline_lines()
{
    if (!floorHW)
        return SCREEN_YSIZE;
    for (int s = 0; s < 4; ++s)
        if (activeTileLayers[s] < LAYER_COUNT && stageLayouts[activeTileLayers[s]].type == LAYER_3DSKY)
            return (SCREEN_YSIZE / 2) + 12 + flr_first_row(&stageLayouts[activeTileLayers[s]]);
    return SCREEN_YSIZE;
}
#define PM_HLINE_LINES pm_hline_lines()
#define PM_VERTS ((Vertex *)(0x24000000 + PM_FB_VERTS))
#define PM_SCAN ((int *)(0x24000000 + PM_FB_SCAN))
#define PM_LIST pmList
/* REV R4D: the second SH2 (the Options Menu's 2ND SH2 switch, scdSh2Switch) - the floor, the tile layers and the big
   faces of the picture mode drawn half by it (scd_pm.h PM_SPLIT: every other row; the same picture). The loader's
   secondary() (cd32x/sh2_engboot.c) waits for a job in the mailbox after the boot record (cache-through): 'SLV1', a
   sequence number, the function, its argument; done = the sequence once it returned (its cache purged before each:
   this SH2 changed what it reads). Its half goes first, then this SH2 draws its own and waits for the other: at most 30
   vertical blanks - then it draws that half too and leaves the second SH2 alone. The first job is a test (none from a
   loader without the mailbox: boot record [11] 'SLV1'). Status codes: 5C1B the second SH2 answers, 5C1C it does not. */
#define SLAVE_BOX ((volatile unsigned int *)0x26001F40)
#define SLAVE_MAGIC 0x534C5631u
static int slaveState = 0; /* 0 not tried yet, 1 it answers, -1 not there */
static unsigned int slaveSeq = 0;
static void slave_post(void (*fn)(unsigned int), unsigned int arg)
{
    SLAVE_BOX[2] = (unsigned int)fn;
    SLAVE_BOX[3] = arg;
    SLAVE_BOX[0] = SLAVE_MAGIC;
    SLAVE_BOX[1] = ++slaveSeq; /* (last: the second SH2 starts when it sees it) */
}
static bool slave_wait(unsigned int vbls)
{
    const unsigned int v0 = VBLANK_COUNT;
    while (SLAVE_BOX[4] != slaveSeq) {
        if (VBLANK_COUNT - v0 > vbls)
            return false;
        unsigned int k = 64; /* about 250 cycles in registers only: the bus stays free for the second SH2 */
        __asm__ __volatile__("0:\n\tdt %0\n\tbf 0b" : "+r"(k) : : "t");
    }
    return true;
}
static void slave_nop(unsigned int) {}
static bool slave_ready()
{
    if (!scdSh2Switch || slaveState < 0)
        return false;
    if (slaveState == 0) {
        if (SCD_BOOT_RECORD[11] != SLAVE_MAGIC)
            slaveState = -1;
        else {
            slave_post(slave_nop, 0);
            slaveState = slave_wait(3) ? 1 : -1;
        }
        STATE(slaveState > 0 ? 0x5C1B : 0x5C1C);
    }
    return slaveState > 0;
}
#define PM_SPLIT_FN(fn, J)                                                                                              \
    static void fn##_slave(unsigned int a) { fn((const J *)a, 1, 2); }
#define PM_SPLIT(fn, job)                                                                                               \
    do {                                                                                                                \
        if (slave_ready()) {                                                                                            \
            slave_post(fn##_slave, (unsigned int)(job));                                                                \
            fn(job, 0, 2);                                                                                              \
            if (!slave_wait(30)) {                                                                                      \
                slaveState = -1;                                                                                        \
                fn(job, 1, 2);                                                                                          \
            }                                                                                                           \
        }                                                                                                               \
        else                                                                                                            \
            fn(job, 0, 1);                                                                                              \
    } while (0)
#include "scd_pm.h"
static void pad_service(); /* REV R4D: the pad during a long picture (below) */
void scd_pm_clear(int index)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    pm_clear(index);
}
void scd_pm_sprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int direction, int sheetID, int alpha)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    if (floorHW && flrTitle && sprX == 257 && sprY == 146 && width == 120 && height == 104)
        return; /* REV R5E: the title's planet - on the Genesis plane behind the clouds (FLOOR9.BIN) */
    pm_sprite(XPos, YPos, width, height, sprX, sprY, direction, sheetID, alpha);
}
void scd_pm_sprite_scaled(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX,
                          int sprY, int sheetID)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    pm_sprite_scaled(direction, XPos, YPos, pivotX, pivotY, scaleX, scaleY, width, height, sprX, sprY, sheetID);
}
void scd_pm_sprite_rotozoom(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation,
                            int scale, bool zoom, int sheetID)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    pm_sprite_rotozoom(direction, XPos, YPos, pivotX, pivotY, sprX, sprY, width, height, rotation, scale, zoom, sheetID);
}
void scd_pm_rect(int XPos, int YPos, int width, int height, int R, int G, int B, int A)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    if (floorHW && flrTitle && XPos <= 0 && YPos <= 0 && XPos + width >= SCREEN_XSIZE && YPos + height >= SCREEN_YSIZE && A >= 0xFF) {
        /* REV R5E: the title's sky - the Genesis backdrop shows it (behind the chip's clouds and the planet); the 32X
           picture colour 0 (see-through) where the rectangle was */
        ttlSky = (unsigned int)(R & 0xFF) << 16 | (unsigned int)(G & 0xFF) << 8 | (unsigned int)(B & 0xFF);
        PM_FLUSH();
        if (pmSkip)
            return;
        volatile unsigned int *p = (volatile unsigned int *)(0x24000000u + PM_FB_PIXELS);
        for (unsigned int i = 0; i < (unsigned int)(PM_STRIDE * PM_LINES) / 4; ++i) p[i] = 0;
        return;
    }
    pm_rect(XPos, YPos, width, height, R, G, B, A);
}
void scd_pm_face(void *v, unsigned int colour)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    pm_face(v, colour);
}
void scd_pm_tex_face(void *v, int sheetID)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    pm_tex_face(v, sheetID);
}
void scd_pm_draw3dscene(int sheetID)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    if (floorHW && flrTitle) { /* REV R5E: the title's clouds - the chip's (matFinal as the engine leaves it) */
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) matFinal.values[y][x] = matWorld.values[y][x];
        MatrixMultiply(&matFinal, &matView);
        PM_FLUSH();
        if (pmSkip || title_hw_frame((-matWorld.values[3][2]) & 2047))
            return;
    }
    pm_draw3dscene(sheetID);
}
/* REV R5: THE SPECIAL STAGES' FLOOR ON THE SEGA CD'S GRAPHICS CHIP - as the 1993 game: the Sub-CPU's command 80
   (cd32x/sub_main.c) draws the floor with the stamp / rotation chip (FLOOR<n>.BIN, tools/build_floor.py) and the
   68000 shows it on the Genesis window plane (lines 120-223). The SH2 sends the floor layer's position and angle for
   each picture it draws, clears the floor's rows of its own picture to colour 0 (the 32X's colour 0 is see-through
   then: the Genesis shows), and sends the floor's 15 colours (the floor file's palette indices, with the fade) when
   they change. No floor file, or no answer: the SH2 draws the floor itself, as before. */
static int sd_cmd(unsigned short cmd) SCD_HOT_NOINLINE;
static int sd_cmd_long(unsigned short cmd) SCD_HOT_NOINLINE;
static int flrYSent; /* REV R5B: the height the Sub-CPU has */
static bool name_is(const char *a, const char *b); /* (below) */
static void floor_hw_begin() /* (pm_begin) a special stage: the chip's floor */
{
    floorHW  = false;
    flrTitle = false;
    int stage = 0;
    if (activeStageList == STAGELIST_SPECIAL && stageListPosition >= 0 && stageListPosition < stageListCount[STAGELIST_SPECIAL]) {
        const char *f = stageList[STAGELIST_SPECIAL][stageListPosition].folder; /* "SS1" - "SS8" */
        if ((f[0] == 'S' || f[0] == 's') && (f[1] == 'S' || f[1] == 's') && f[2] >= '1' && f[2] <= '8' && !f[3])
            stage = f[2] - '0';
    }
    else if (activeStageList == STAGELIST_PRESENTATION && stageListPosition == 0 && stageListCount[STAGELIST_PRESENTATION] > 0 &&
             name_is(stageList[STAGELIST_PRESENTATION][0].folder, "Title"))
        stage = 9; /* REV R5E: the title - its clouds */
    if (!stage)
        return;
    comm_idle();
    SC_COMM2  = (unsigned short)(stage << 8 | 0);
    SC_COMM12 = 0xFFFF;
    sd_cmd_long(80);
    if (SC_COMM12 != 1)
        return;
    for (int k = 0; k < 32; ++k) {
        flrIdx[k] = 0;
        if ((k >= 1 && k < 16) || (stage == 9 && k >= 17)) { /* (the title: the planet's colours 17-31 too) */
            SC_COMM2  = (unsigned short)(k << 8 | 6);
            flrIdx[k] = sd_cmd(80) < 0 ? 0 : (unsigned short)(SC_COMM8_32 & 0xFF);
        }
        flrGen[k]  = 0;
        flrSent[k] = 0xFFFF;
    }
    flrTitle = stage == 9;
    flrYSent = (int)0x80000000; /* (none sent yet) */
    floorHW    = true;
    genesisOff = false; /* (the Sub-CPU switched the Genesis display on) */
}
static void floor_hw_end() /* (pm_end) */
{
    if (!floorHW)
        return;
    comm_idle();
    SC_COMM2 = 7;
    sd_cmd_long(80);
    floorHW    = false;
    flrTitle   = false;
    genesisOff = false; /* (R5B: the Sub-CPU's set-up after it - command 68's off - leaves the display on, blank) */
}
static bool floor_hw_frame(int layerID) /* instead of pm_sky: the floor's values to the Sub-CPU, its rows see-through;
                                           false (R5B): the Sub-CPU ended the floor - pm_sky draws it */
{
    PM_FLUSH();
    if (pmSkip)
        return true;
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return true;
    comm_idle();
    for (int k = 1; k < 16; k += 2) /* the colours that changed (pm_colours: flrGen), two a command */
        if (flrGen[k] != flrSent[k] || (k < 15 && flrGen[k + 1] != flrSent[k + 1])) {
            SC_COMM2    = (unsigned short)(k << 8 | 5);
            SC_COMM8_32 = (unsigned int)flrGen[k] << 16 | (k < 15 ? flrGen[k + 1] : 0);
            if (sd_cmd(80) == 0) {
                flrSent[k] = flrGen[k];
                if (k < 15)
                    flrSent[k + 1] = flrGen[k + 1];
            }
        }
    /* REV R5B: X and Z in one command (13.3 each: the chip's own precision), the height only when it changed */
    int fx = layer->XPos >> 13, fz = layer->ZPos >> 13;
    fx = fx < -32768 ? -32768 : fx > 32767 ? 32767 : fx;
    fz = fz < -32768 ? -32768 : fz > 32767 ? 32767 : fz;
    SC_COMM2    = 1;
    SC_COMM8_32 = (unsigned int)(fx << 16) | ((unsigned int)fz & 0xFFFF);
    sd_cmd(80);
    if (layer->YPos != flrYSent) {
        SC_COMM2    = 3;
        SC_COMM8_32 = (unsigned int)layer->YPos;
        if (sd_cmd(80) == 0)
            flrYSent = layer->YPos;
    }
    SC_COMM2    = 4;
    SC_COMM8_32 = (unsigned int)(sinMLookupTable[layer->angle & 0x1FF] << 16) | (cosMLookupTable[layer->angle & 0x1FF] & 0xFFFF);
    SC_COMM12   = 0xFFFF;
    if (sd_cmd(80) == 0 && SC_COMM12 == 0) { /* R5B: ended on the Sub-CPU's side (another command): the SH2's floor */
        floorHW    = false;
        genesisOff = false;
        return false;
    }
    /* the floor's rows (124-223, pm_sky_part's) colour 0 in the SH2's buffer (the frame buffer itself: the overwrite
       image skips 0) */
    const int row0 = (PM_LINES / 2) + 12 + flr_first_row(layer); /* REV R5B: the rows the chip draws */
    if (row0 > (PM_LINES / 2) + 12)
        pm_sky_rows(layerID, row0); /* REV R5B: the rows past the map's edge, as before (after the colours: no pmSkip) */
    volatile unsigned int *p = (volatile unsigned int *)(0x24000000u + PM_FB_PIXELS + PM_STRIDE * row0);
    for (unsigned int i = 0; i < (unsigned int)(PM_STRIDE * (PM_LINES - row0)) / 4; ++i) p[i] = 0;
    return true;
}
/* REV R5E: THE TITLE'S CLOUDS ON THE CHIP - each picture: the colours that changed (CRAM 0-31: the sky, the clouds' line
   0, the planet's line 1), the scroll (the clouds object's Draw3DScene: its world translation's z is -scroll), then the
   chip draws. false: the Sub-CPU ended it - the SH2 draws the clouds itself */
static bool title_hw_frame(int v32)
{
    comm_idle();
    for (int k = 0; k < 32; k += 2)
        if (flrGen[k] != flrSent[k] || flrGen[k + 1] != flrSent[k + 1]) {
            SC_COMM2    = (unsigned short)(k << 8 | 5);
            SC_COMM8_32 = (unsigned int)flrGen[k] << 16 | flrGen[k + 1];
            if (sd_cmd(80) == 0)
                flrSent[k] = flrGen[k], flrSent[k + 1] = flrGen[k + 1];
        }
    SC_COMM2    = 1;
    SC_COMM8_32 = (unsigned int)v32;
    sd_cmd(80);
    SC_COMM2    = 4;
    SC_COMM8_32 = 0;
    SC_COMM12   = 0xFFFF;
    if (sd_cmd(80) == 0 && SC_COMM12 == 0) {
        floorHW    = false;
        flrTitle   = false;
        genesisOff = false;
        return false;
    }
    return true;
}
void scd_pm_sky(int layerID)
{
    pmDrew = true;
    pad_service(); /* REV R4D */
    if (floorHW && floor_hw_frame(layerID))
        return;
    pm_sky(layerID);
}
void scd_pm_copy_tile(int dest, int src)
{
    if (aniTiles) { /* REV R4C: a planes act - on the map only if the picture has name words (md_tile_copy) */
        if (md_tile_copy(pmTileMap, (uint8_t *)aniDirty, aniTiles, dest, src))
            aniDirtyAny = true;
        return;
    }
    pm_copy_tile(dest, src);
}
void scd_pm_stage_new()
{
    pm_tiles_reset();
    aniTiles    = 0; /* REV R4C: (planes_load: the act file's) */
    aniDirtyAny = false;
}
static void pm_layer_end(int slot) __attribute__((noinline)); /* cart code (plat_layer_end) */
static void pm_layer_end(int slot)
{
    if (slot >= 0 && slot < 4) {
        pmDrew = true;
        pad_service(); /* REV R4D */
        pm_hline(slot, pmTileY[slot], pmLines);
    }
}
static int pm_darkest() /* the palette index nearest black: the picture's colour where nothing is drawn (the engine's 0) */
{
    const unsigned short *pal = fullPalette[gfxLineBuffer[0]];
    int best = 0, bestV = 0x7FFFFFFF;
    for (int i = 0; i < 256; ++i) {
        unsigned short c = pal[i];
        int v            = (c >> 11) * 3 + ((c >> 5) & 63) * 2 + (c & 31);
        if (v < bestV)
            bestV = v, best = i;
    }
    return best;
}
static void pm_buffer_setup(int k) /* the SH2's frame buffer: the line table, the picture all colour k */
{
    volatile unsigned short *fb = SC_FB;
    for (int y = 0; y < 256; ++y) fb[y] = (unsigned short)(PM_FB_PIXELS / 2 + (y < 224 ? y : 223) * (PM_STRIDE / 2));
    unsigned short w = (unsigned short)(k << 8 | k);
    for (int i = 0; i < PM_STRIDE * PM_LINES / 2; ++i) PM_FB16[i] = w;
}
static unsigned int pm_pool_used() /* the layout pool's bytes in use (each layer: its rows' entries, then their scroll) */
{
    unsigned int used = 0, base = 0x24000000u + PM_FB_POOL;
    for (int i = 0; i < LAYER_COUNT; ++i) {
        unsigned int t = (unsigned int)(unsigned long)stageLayouts[i].lineScroll;
        if (stageLayouts[i].rowsCap > 0 && t >= base && t < base + PM_POOL_BYTES) {
            unsigned int end = t + (unsigned int)stageLayouts[i].rowsCap * CHUNK_SIZE - base;
            if (end > used)
                used = end;
        }
    }
    return (used + 3u) & ~3u;
}
static void pm_mirror(unsigned int at, unsigned int bytes) __attribute__((noinline));
static void pm_mirror(unsigned int at, unsigned int bytes) /* frame buffer bytes at..: the SH2's buffer into the other, 2 KB
                                                              at a time through the stack - two swaps each */
{
    unsigned short buf[1024];
    for (unsigned int done = 0; done < bytes; done += sizeof(buf)) {
        unsigned int n             = bytes - done < sizeof(buf) ? bytes - done : (unsigned int)sizeof(buf);
        volatile unsigned short *p = (volatile unsigned short *)(0x24000000u + at + done);
        for (unsigned int i = 0; i < n / 2; ++i) buf[i] = p[i];
        flip_wait();
        for (unsigned int i = 0; i < n / 2; ++i) p[i] = buf[i];
        flip_wait();
    }
}
static void pm_begin() __attribute__((noinline));
static void pm_begin() /* plat_stage_loaded: the scene is loaded - both frame buffers set up, the display off until it draws */
{
    STATE(0x5C90);
    splash_part(20, 32); /* the loading bar full (the scene has no act file) */
    splash_chunk(1, 1);
    if (!genesisOff) {
        sub_cmd(52, 0); /* the Genesis display off: the 32X picture is the whole screen */
        genesisOff = true;
    }
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* blank while the buffers are set up */
    splashUp    = false;
    skyPicture  = false; /* (both buffers are the picture's now) */
    statusReady = false;
    int k       = pm_darkest();
    pm_buffer_setup(k);
    pm_mirror(PM_FB_POOL, pm_pool_used()); /* the layouts into the other buffer */
    flip_wait();
    pm_buffer_setup(k); /* the other buffer: the first frame is drawn into it */
    pmReady     = true;
    pmShown     = false;
    pmDrew      = false;
    pmSkip      = false;
    pmSkipRun   = 0;
    pmLogic     = 0;
    pmVbl0      = VBLANK_COUNT;
    layoutQn    = 0;
    layoutQLost = false;
    pmPalGen++;
    floor_hw_begin(); /* REV R5: a special stage - its floor on the Sega CD's graphics chip */
    STATE(0x5C91);
}
static void pm_end(bool picture) /* the scene ends (another loads: the loading picture; the dev menu: blank) */
{
    floor_hw_end(); /* REV R5 */
    if (!scdPicture && !pmReady)
        return;
    bool shown = pmShown;
    scdPicture = false;
    pmReady    = false;
    pmShown    = false;
    if (shown && picture) {
        big_home_w1(); /* REV R4J: window 1 to page 1 first - the loading picture is in the data pack there; the menu
                          (pages 6 and 7) leaves window 1 on page 7, and the picture then did not come (the save
                          screen stayed, faded, through the whole preload) */
        splash_show(); /* (into the buffer shown last: the SH2 keeps its own) */
    }
    else
        MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000);
    statusReady = false;
    skyPicture  = false;
}
static void pm_colours() /* the frame's palette with its fade (the engine's arithmetic: DrawRectangle) into the 32X colours */
{
    const unsigned short *pal = fullPalette[gfxLineBuffer[0]];
    int a                     = pmFadeA > 0xFF ? 0xFF : pmFadeA, ia = 0xFF - a;
    unsigned int clr = (unsigned int)((((pmFadeB & 0xFF) >> 3) | (((pmFadeG & 0xFF) >> 2) << 5) | (((pmFadeR & 0xFF) >> 3) << 11)));
    int fr = (int)(clr >> 11), fg = (int)((clr & 0x7E0) >> 6), fb = (int)(clr & 0x1F);
    volatile unsigned short *cram = SC_CRAM;
    for (int i = 0; i < 256; ++i) {
        unsigned int c = pal[i];
        int r = (int)(c >> 11), g = (int)((c >> 6) & 31), b = (int)(c & 31);
        if (a > 0) {
            r = (ia * r >> 8) + (a * fr >> 8);
            g = (ia * (int)((c & 0x7E0) >> 6) >> 8) + (a * fg >> 8);
            b = (ia * b >> 8) + (a * fb >> 8);
        }
        unsigned short w = (unsigned short)(0x8000 | b << 10 | g << 5 | r); /* bit 15: in front of the Genesis */
        cram[i]          = w == 0x8000 ? 0x8400 : w;                         /* (as the loading picture: never 0x8000) */
    }
    if (floorHW) { /* REV R5: colour 0 see-through (the Genesis floor shows); the floor's colours for the Genesis */
        cram[0] = 0x0000;
        /* REV R5D: the floor's lines' palette (the title: line 0's) */
        const unsigned short *fpal = flrTitle ? pal : fullPalette[gfxLineBuffer[(SCREEN_YSIZE / 2) + 12]];
        const int n = flrTitle ? 32 : 16;
        for (int k = 0; k < n; ++k) {
            unsigned int c;
            if (k == 0 || k == 16) { /* (the title: CRAM 0 the sky - its rectangle's colour; 16 unused) */
                if (!flrTitle || k == 16) {
                    flrGen[k] = 0;
                    continue;
                }
                c = ((ttlSky >> 19) & 31) << 11 | ((ttlSky >> 10) & 63) << 5 | ((ttlSky >> 3) & 31);
            }
            else
                c = fpal[flrIdx[k]];
            int r = (int)(c >> 11), g = (int)((c >> 6) & 31), b = (int)(c & 31);
            if (a > 0) {
                r = (ia * r >> 8) + (a * fr >> 8);
                g = (ia * (int)((c & 0x7E0) >> 6) >> 8) + (a * fg >> 8);
                b = (ia * b >> 8) + (a * fb >> 8);
            }
            flrGen[k] = (unsigned short)((b >> 2) << 9 | (g >> 2) << 5 | (r >> 2) << 1);
        }
    }
}
static void pm_present() __attribute__((noinline)); /* cart code: plat_present (SDRAM) calls it */
static void pm_present()
{
    pm_frame_end();
    pmLogic++;
    bool flip = !pmSkip && (pmShown || pmDrew); /* nothing drawn yet: the display waits, the same buffer again */
    if (flip) {
        /* the scripts' layout changes (plat_layout_set): their values from this buffer, into the other after the swap */
        unsigned short val[LAYOUT_QUEUE];
        for (int i = 0; i < layoutQn; ++i) {
            const TileLayer *L = &stageLayouts[layoutQ[i][0] < LAYER_COUNT ? layoutQ[i][0] : 0];
            val[i]             = layoutQ[i][2] < L->rowsCap ? L->tiles[layoutQ[i][1] + (layoutQ[i][2] << 8)] : 0;
        }
        if (layoutQLost) /* more changes in one frame than the queue holds: all the layouts into the other buffer -
                            from this one (the newest), so before the swap (the review: after it, the old ones won) */
            pm_mirror(PM_FB_POOL, pm_pool_used());
        flip_wait(); /* the frame is shown from this vertical blank */
        pm_colours();
        if (!pmShown) {
            MARS_VDP_DISPMODE = (unsigned short)((MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256);
            pmShown           = true;
        }
        if (!layoutQLost)
            for (int i = 0; i < layoutQn; ++i) {
                const TileLayer *L = &stageLayouts[layoutQ[i][0] < LAYER_COUNT ? layoutQ[i][0] : 0];
                if (layoutQ[i][2] < L->rowsCap)
                    L->tiles[layoutQ[i][1] + (layoutQ[i][2] << 8)] = val[i];
            }
        layoutQn    = 0;
        layoutQLost = false;
        pmDrew      = false;
    }
    else {
        unsigned int target = pmVbl0 + pmLogic; /* logic only: on time, not ahead of it */
        if ((int)(VBLANK_COUNT - target) < 0) {
            while ((int)(VBLANK_COUNT - target) < 0) {
            }
            if (pmShown)
                pm_colours(); /* (a vertical blank began just now: the fade goes on between the pictures) */
        }
    }
    pmPalGen++;
    int late = (int)(VBLANK_COUNT - (pmVbl0 + pmLogic));
    if (late > 90) { /* far behind (the game itself is slower than 60 frames a second here): from now on */
        pmVbl0 = VBLANK_COUNT - pmLogic;
        late   = 0;
    }
    if (late > 0 && pmShown && pmSkipRun < PM_SKIP_MAX) {
        pmSkip = true;
        pmSkipRun++;
    }
    else {
        pmSkip    = false;
        pmSkipRun = 0;
    }
}
#else
void plat_menu_clear() {}
void plat_menu_text(const unsigned short *, int, int, int, int) {}
void plat_layout_set(int, int, int) {} /* REV R4B: no planes - the engine draws its layers itself */
void scd_text_on() {}
bool scdPicture = false; /* REV R4C: the picture mode is the planes build's */
void scd_pm_clear(int) {}
void scd_pm_sprite(int, int, int, int, int, int, int, int, int) {}
void scd_pm_sprite_scaled(int, int, int, int, int, int, int, int, int, int, int, int) {}
void scd_pm_sprite_rotozoom(int, int, int, int, int, int, int, int, int, int, int, bool, int) {}
void scd_pm_rect(int, int, int, int, int, int, int, int) {}
void scd_pm_face(void *, unsigned int) {}
void scd_pm_tex_face(void *, int) {}
void scd_pm_draw3dscene(int) {}
void scd_pm_sky(int) {}
void scd_pm_copy_tile(int, int) {}
void scd_pm_stage_new() {}
#endif
bool plat_boot_menu() /* REV R4 */
{
#if SCD_PLANES
#if defined(SCD32X_START_LIST) && SCD32X_START_LIST == 0 && SCD32X_START_STAGE == 0
    return false; /* REV R4C: a disc that starts with the title screen starts the game itself (A + B + C + START: the
                     Options Menu) */
#else
    return SCD_DEVMENU != 0;
#endif
#else
    return false;
#endif
}
#if SCD32X_DRAWCART
static void draw_show(); // S7e (below)
#endif
void plat_present(const unsigned short *, int, int, int) SCD_HOT; /* R1V: SDRAM */
void plat_present(const unsigned short *, int, int, int)
{
#if SCD_PLANES
    if (scdPicture && pmReady) { /* REV R4C: the 32X picture mode */
        pm_present();
        presented++;
        return;
    }
#endif
#if SCD32X_DRAWCART
    draw_show(); /* S7e: the game's own picture; flip_wait paces at the vertical blank */
#if SCD_DRAW_DIAG
    { /* S7e7: one reading per second on line 10, in turn: Fnnn frames shown so far, Ennn not drawn / 64,
         D/C + line size (frames can take longer than a second: then one reading per frame) */
        static unsigned int secStart = 0, second = 0;
        if (!secStart)
            secStart = VBLANK_COUNT;
        if (VBLANK_COUNT - secStart >= 60) {
            unsigned int k = second++ % 3;
            if (k == 0)
                STATE(0xF000 | ((presented + 1) & 0xFFF));
            else if (k == 1)
                STATE(0xE000 | ((diagUntouched >> 6) > 0xFFF ? 0xFFF : (diagUntouched >> 6)));
            else
                STATE(((Engine.frameBuffer == plat_frame_target()) ? 0xD000 : 0xC000) | (GFX_LINESIZE & 0xFFF));
            secStart = VBLANK_COUNT;
        }
    }
#endif
#else
#if SCD_PLANES
    if (menuDrawn)
        menu_frame(); /* REV R4: the dev menu drew its text this frame (cart code: the menu needs no speed) */
    else if (planesOn)
        planes_frame(); /* REV R1 */
#endif
    unsigned int f = VBLANK_COUNT;
    while (VBLANK_COUNT == f) {
    } /* pace: at most one engine frame per vertical blank */
#if SCD_PLANES
    if (planesOn && !menuDrawn)
        sky_vblank(); /* REV R4B: the 32X sky's colours for the frame the 68000 shows from this vertical blank */
#endif
#endif
    presented++;
    /* S7d: no 4B10 - Kobo's Sub-CPU stops reprinting line 10 after it, and line 10 has to keep
       showing the scene changes (STATE:5Lnn, plat_stage_loaded) */
}
// S7e: the engine's renderer draws into the 32X frame buffer (the back buffer, always seen at
// 0x24000000): line table at 0, pixels from byte 0x200 in the engine's 328-pixel lines, 198 rows;
// then a black line (0x0400) for the rows above and below the picture
#define DRAW_ROWS 198
#define DRAW_TOP 13 /* (224 - 198) / 2 */
#define DRAW_PIXELS 0x100 /* words */
#define DRAW_BLACK (DRAW_PIXELS + 328 * DRAW_ROWS)
unsigned short *plat_frame_target() { return (unsigned short *)(SC_FB + DRAW_PIXELS); }
#if SCD32X_DRAWCART
#define DRAW_MARKER 0x0841 /* S7e4: written before each engine frame; still there = the engine did not draw it */
static void draw_show() /* convert to the 32X's 15-bit colours, line table, show (Kobo's splash format) */
{
    static int shown = 0;
    volatile unsigned short *fb = SC_FB;
    const int stride            = GFX_LINESIZE;
#if SCD_DRAW_DIAG
    {
        unsigned int n = 0; /* pixels the engine did not write since the marker fill */
        for (int y = 0; y < DRAW_ROWS; ++y) {
            volatile unsigned short *p = fb + DRAW_PIXELS + y * stride;
            for (int x = 0; x < 320; ++x) n += p[x] == DRAW_MARKER;
        }
        diagUntouched = n;
    }
#endif
    for (int y = 0; y < DRAW_ROWS; ++y) {
        volatile unsigned short *p = fb + DRAW_PIXELS + y * stride;
        for (int x = 0; x < 320; ++x) {
            unsigned short c = p[x];
            unsigned short o = (unsigned short)(((c & 0x1F) << 10) | ((c >> 1) & 0x3E0) | (c >> 11));
            p[x]             = o ? o : 0x0400; /* 0x0000 is see-through on the 32X (Kobo) */
        }
    }
    for (int i = 0; i < 320; ++i) fb[DRAW_BLACK + i] = 0x0400;
    for (int y = 0; y < 224; ++y) {
        int src = y - DRAW_TOP;
        fb[y]   = (unsigned short)((src >= 0 && src < DRAW_ROWS) ? DRAW_PIXELS + src * stride : DRAW_BLACK);
    }
    if (!shown) {
        shown = 1;
#if !SCD_DRAW_DIAG
        /* S7e3 (OpenJazz addendum, frame buffer rule 5; Kobo / OpenJazz at the splash): the Genesis
           display OFF when the game's picture appears - with it on behind the 32X, real hardware washes
           the picture out. CMD 52, COMM2 = 0; the diagnostics stay visible up to this moment. */
        *(volatile unsigned short *)0x20004022 = 0;  /* COMM2 */
        *(volatile unsigned short *)0x20004020 = 52; /* COMM0: the command */
        for (unsigned int n = 0; n < 800000u && *(volatile unsigned short *)0x20004020 != 0; ++n) {
            for (volatile int k = 0; k < 64; ++k) {
            } /* HW6: pause between COMM reads */
        }
#endif
        MARS_VDP_DISPMODE = (MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_32K; /* Kobo's splash mode */
    }
    flip_wait();
#if SCD_DRAW_DIAG
    for (int y = 0; y < DRAW_ROWS; ++y) { /* the marker into the new back buffer, before the engine draws */
        volatile unsigned short *p = fb + DRAW_PIXELS + y * stride;
        for (int x = 0; x < 320; ++x) p[x] = DRAW_MARKER;
    }
#endif
}
#endif
// REV R4: the controller (pad 1). The 68000's vertical-blank handler reads both pads into its comm words (hw_md.s);
// the Sub-CPU answers SH2 command 48 with pad 1 in COMM8's high word (SEGA_CTRL_* bits, sub_main.c's fast path) -
// Kobo's pad path, unchanged. One request a frame, answered while the engine runs: each frame takes the answer to
// the previous frame's request (a frame late, as most Genesis games read their pads). COMM0 is one register: every
// other command waits for that answer first (comm_idle).
static bool padPending     = false;
static unsigned int padNow = 0;
static unsigned int padRaw = 0xF000; /* REV R4: the answer as it came (SEGA_CTRL_* bits; 0xF000 none) - the credits read X, Z */
static void pad_take() /* the answer: SEGA_CTRL_* -> the engine's PAD_* (a six-button pad's X, Y, Z as A, B, C) */
{
    unsigned int s = SC_COMM8_32 >> 16, p;
    padPending     = false;
    padRaw         = s;
    if ((s & 0xF000) == 0xF000) { /* SEGA_CTRL_NONE: no pad in port 1 */
        padNow = 0;
        return;
    }
    p = s & 0x0F; /* up, down, left, right */
    if (s & 0x0440)
        p |= PAD_A; /* A, X */
    if (s & 0x0210)
        p |= PAD_B; /* B, Y */
    if (s & 0x0120)
        p |= PAD_C; /* C, Z */
    if (s & 0x0080)
        p |= PAD_START;
    padNow = p;
}
static bool comm0_free() { return SC_COMM0 == 0 && SC_COMM0 == 0; } /* twice: a read during the 68000's write is undefined */
static bool musicInFlight = false; /* REV R4: a music command (45 / 54) sent, not waited for */
static void comm_idle()                                             /* COMM0 free for another command */
{
    for (unsigned int n = 0; n < 2000000u && !comm0_free(); ++n) sgb_pause();
    if (padPending)
        pad_take();
    musicInFlight = false;
}
// ---------------------------------------------------------------- REV R4: music and sound effects
// Music: the game's tracks are CD audio tracks on the disc (tools/build_audio.py: the JP soundtrack and the past
// tracks, from the game's own files; engine/CDTracks.inc maps each file name - US names to the same tracks - to its
// track number and length). The Sub-CPU plays a track over and over (command 45) and stops (54); a track the game does
// not loop (the act's end, game over) is stopped after its length. The commands go out in the pad request's place,
// not waited for: no frame waits for the CD drive.
// Sound effects: the Sega CD's PCM chip, from the Sub-CPU (Kobo's path: KOBOSFX.BIN loaded once by command 53; an effect
// number in the upper byte of the pad request plays it). engine/SFXBank.inc maps the game's effect file names to the
// bank's numbers; each effect's PCM step comes from its sample rate and the chip's own clock (32,552 Hz at step 0x800),
// so it plays at the game's pitch. One effect a frame; up to 4 wait.
#if __has_include("../../engine/CDTracks.inc")
struct ScdTrack {
    const char *name;
    unsigned char track;
    unsigned short seconds;
};
#include "../../engine/CDTracks.inc" /* scdTracks[], scdTrackCount (tools/build_audio.py; game data - not published) */
#define SCD_HAVE_TRACKS 1
#else
#define SCD_HAVE_TRACKS 0
#endif
#if __has_include("../../engine/SFXBank.inc")
struct ScdSfx {
    const char *name;
    unsigned char id;
};
#include "../../engine/SFXBank.inc" /* scdSfxBank[], scdSfxBankCount */
#define SCD_HAVE_SFX 1
#else
#define SCD_HAVE_SFX 0
#endif
static void sub_cmd(unsigned short cmd, unsigned short arg) /* a Sub-CPU command, waited for (45 play, 54 stop, 53 ...) */
{
    comm_idle();
    SC_COMM2 = arg;
    SC_COMM0 = cmd;
    for (unsigned int n = 0; n < 4000000u && !comm0_free(); ++n) sgb_pause();
}
static bool creditsMusic = false;                 /* a track the credits screen started may still play */
static unsigned short musicCmd = 0, musicArg = 0; /* the music command to send (0 none) */
static unsigned char musicTrack = 0;              /* the track playing (0 none) */
static unsigned int musicStopAt = 0;              /* the vertical blank a track the game does not loop ends at (0: loops) */
static unsigned char sfxMap[0x100];               /* the engine's effect number -> the bank's (0: none) */
static unsigned char sfxQueue[4], sfxQueued = 0;
static bool sfxBankLoaded = false;
#ifdef SCD_SFX_BANKS
/* REV R4D: the scenes' own banks (the Options Menu's STAGE SFX switch, scdSfxSwitch): the engine's effect number -> its
   name in scdSfxNames (0xFF: none); the bank's own number is looked up when it plays (scdSfxBankMap: KOBOSFX.BIN is bank
   0 - the switch off plays exactly R4C's numbers) */
static unsigned char sfxName[0x100];            /* (the name's index + 1; 0: none) */
static unsigned char sfxBank = 0;                 /* the bank in the PCM chip (0 KOBOSFX.BIN, n SFX<n>.BIN) */
#endif
static bool name_is(const char *a, const char *b) /* the same name, any case */
{
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z')
            x = (char)(x - 32);
        if (y >= 'a' && y <= 'z')
            y = (char)(y - 32);
        if (x == '\\')
            x = '/';
        if (y == '\\')
            y = '/';
        if (x != y)
            return false;
        if (!x)
            return true;
    }
}
static void music_send(unsigned short cmd, unsigned short arg) /* the latest wins (a stop after a play: nothing plays) */
{
    musicCmd = cmd;
    musicArg = arg;
}
void plat_music_play(const char *name, int loop, unsigned int)
{
#if SCD_HAVE_TRACKS
    for (int i = 0; i < scdTrackCount; ++i)
        if (name_is(name, scdTracks[i].name)) {
            music_send(45, scdTracks[i].track);
            musicTrack  = scdTracks[i].track;
            musicStopAt = loop ? 0 : (VBLANK_COUNT + 60u * scdTracks[i].seconds + 30u) | 1u;
            return;
        }
#endif
    plat_music_stop(); /* a track the disc does not have: silence */
}
void plat_music_stop()
{
    if (musicTrack || musicCmd == 45) {
        music_send(54, 0);
        musicTrack  = 0;
        musicStopAt = 0;
    }
}
void plat_sfx_loaded(int sfx, const char *name, int, int, int, int) /* the engine loaded effect number sfx (its file name) */
{
    if (sfx < 0 || sfx >= 0x100)
        return;
    sfxMap[sfx] = 0;
#ifdef SCD_SFX_BANKS
    sfxName[sfx] = 0;
    for (int i = 0; i < SCD_SFX_NAMES; ++i)
        if (name_is(name, scdSfxNames[i])) {
            sfxName[sfx] = (unsigned char)(i + 1);
            break;
        }
#endif
#if SCD_HAVE_SFX
    for (int i = 0; i < scdSfxBankCount; ++i)
        if (name_is(name, scdSfxBank[i].name)) {
            sfxMap[sfx] = scdSfxBank[i].id;
            break;
        }
#endif
}
static void audio_before_load() /* REV R4: a scene load reads the CD - the drive stops playing; the effects' bank, once */
{
    musicCmd = 0;
    if (musicTrack || musicInFlight || creditsMusic) {
        sub_cmd(54, 0);
        musicTrack = 0, musicStopAt = 0, creditsMusic = false;
    }
    if (!sfxBankLoaded) {
        SC_COMM12 = 0;
        sub_cmd(53, 0); /* KOBOSFX.BIN -> the PCM chip; COMM12 = the number of effects */
        sfxBankLoaded = SC_COMM12 > 0 && SC_COMM12 < 32;
        STATE(sfxBankLoaded ? 0x5C18 : 0x5C19); /* line 10: the effects are in (5C18) / not on the disc (5C19) */
    }
#ifdef SCD_SFX_BANKS
    /* REV R4D: the STAGE SFX switch on - the scene's own bank (SFX<n>.BIN); off, or the scene has none: KOBOSFX.BIN. A
       bank that does not load leaves the chip as it was (sub_main.c pcm_load): the bank before it plays on */
    int want = 0;
    if (scdSfxSwitch && activeStageList >= 0 && activeStageList < 4 && stageListPosition >= 0 && stageListPosition < 0x80)
        want = scdSfxScene[activeStageList][stageListPosition];
    if (want >= SCD_SFX_BANKS)
        want = 0;
    if (sfxBankLoaded && want != sfxBank) {
        SC_COMM12 = 0;
        sub_cmd(53, (unsigned short)want); /* the bank -> the PCM chip; COMM12 = its number of effects (0: not loaded) */
        if (SC_COMM12 > 0 && SC_COMM12 < 64)
            sfxBank = (unsigned char)want;
        else
            STATE(0x5C1A); /* line 10: the bank did not load */
    }
#endif
    sfxQueued = 0;
}
void plat_sfx_play(int sfx, int)
{
    if (sfx < 0 || sfx >= 0x100 || !sfxBankLoaded)
        return;
#ifdef SCD_SFX_BANKS
    const int n            = sfxName[sfx]; /* REV R4D */
    const unsigned char id = n && n <= SCD_SFX_NAMES ? scdSfxBankMap[sfxBank][n - 1] : 0;
#else
    const unsigned char id = sfxMap[sfx];
#endif
    if (!id)
        return;
    for (int i = 0; i < sfxQueued; ++i)
        if (sfxQueue[i] == id)
            return; /* already waiting */
    if (sfxQueued < 4)
        sfxQueue[sfxQueued++] = id;
}
// ---------------------------------------------------------------- REV R4C: the game's saves in the backup RAM
// MicroNut99's option 2: the Sub-CPU keeps the file SONICCD32X_ (512 bytes) - command 56 reads it into its buffer, 57
// gives one word of the buffer (COMM2 = index, the answer in COMM8), 58 puts one (COMM8 = the value), 59 writes the
// buffer back as the file (cd32x/sub_main.c, Kobo's K36 commands; no new command). The words: cd32x/engine/scd_save.h.
// A save the same as the last one written is not written again (the scripts save at every act's end).
#include "scd_save.h"
static unsigned int saveLastSum = 0; /* FNV-1a of the words last read or written (0: none yet) */
static unsigned int save_fnv(const uint16_t *w)
{
    unsigned int h = 0x811C9DC5u;
    for (int i = 0; i < SCD_SAVE_WORDS; ++i) h = (h ^ w[i]) * 0x01000193u;
    return h | 1u;
}
static unsigned int save_settle() /* COMM0 free and the pad's answer taken: COMM8 carries the save's words now */
{
    /* REV R4C: the loader's sampler off while the words go through COMM8 / COMM12 - the scenes' start-up scripts read
       the save during the stage load (the menu's: ProcessStartupObjects), while it samples (plat_stage_loading turns it
       on): a sample between the Sub-CPU's answer and its reading here garbled a word or COMM12 (the review). Two
       vertical blanks: a sample posted just now is written before the first command. The caller turns it back on. */
    unsigned int was = scd_sampling_now();
    if (was) {
        scd_sampling(0);
        for (int k = 0; k < 2; ++k) {
            unsigned int f = VBLANK_COUNT;
            while (VBLANK_COUNT == f) {
            }
        }
    }
    comm_idle();
    if (padPending)
        pad_take();
    musicInFlight = false;
    return was;
}
static int save_read(uint16_t *w)
{
    SC_COMM12 = 0;
    sub_cmd(56, 0); /* the file into the Sub-CPU's buffer: COMM12 1 = there */
    if (SC_COMM12 != 1)
        return 0;
    for (int i = 0; i < SCD_SAVE_WORDS; ++i) {
        sub_cmd(57, (unsigned short)i);
        w[i] = (uint16_t)(SC_COMM8_32 >> 16);
    }
    return 1;
}
int plat_save_read()
{
    uint16_t w[SCD_SAVE_WORDS];
    unsigned int sampling = save_settle();
    int ok                = save_read(w);
    if (sampling)
        scd_sampling(1);
    if (!ok || !scd_save_unpack(w, saveRAM))
        return 0; /* no file, or not a save of this version: the game starts a new one */
    saveLastSum = save_fnv(w);
    return 1;
}
int plat_save_write()
{
    uint16_t w[SCD_SAVE_WORDS];
    scd_save_pack(saveRAM, w);
    unsigned int h = save_fnv(w);
    if (h == saveLastSum)
        return 1; /* the same as the file: nothing to write */
    unsigned int sampling = save_settle();
    for (int i = 0; i < SCD_SAVE_WORDS; ++i) {
        SC_COMM8_32 = (unsigned int)w[i] << 16;
        sub_cmd(58, (unsigned short)i);
    }
    SC_COMM12 = 0;
    sub_cmd(59, 0); /* the buffer -> the file: COMM12 1 = written */
    int ok = SC_COMM12 == 1;
    if (sampling)
        scd_sampling(1);
    if (!ok)
        return 0;
    saveLastSum = h;
    return 1;
}
#if SCD_DEMO_PAD
// REV R1 test disc: the PC check's pad (tools/run_md_check.sh) - after 4 s, every 4 s: spin dash
// (hold down, press C, release), run right 3 s with one jump - so Sonic moves and the camera scrolls.
// REV R4: it plays only while the dev menu's DEMO PAD switch is on (scdDemoPad), from the stage's start, until a
// button is pressed on the controller.
static unsigned int demo_pad()
{
    unsigned int ms = (presented - scdDemoFrom) * 1000 / 60;
    if (ms < 4000)
        return 0;
    unsigned int t = (ms - 4000) % 4000;
    if (t < 150) return PAD_DOWN;
    if (t < 300) return PAD_DOWN | PAD_C;
    if (t < 900) return PAD_DOWN;
    if (t < 2400) return PAD_RIGHT;
    if (t < 2700) return PAD_RIGHT | PAD_C;
    if (t < 3900) return PAD_RIGHT;
    return 0;
}
#endif
unsigned int plat_pad() SCD_HOT; /* R1W: SDRAM */
/* REV R4D: a picture of the 32X picture mode can take 10 vertical blanks or more (the menus: 3-4 pictures a second), and
   the engine asks for the pad once a frame - a short press between two frames was lost (the menus seemed not to react).
   While the picture mode draws, each draw call asks for the pad again once a vertical blank (command 48 alone: no sound,
   no music command - those wait for plat_pad); what was down goes to the next frame's pad too */
static unsigned int padLatch = 0, padPollVbl = 0;
static void pad_service()
{
    if (VBLANK_COUNT == padPollVbl)
        return;
    padPollVbl = VBLANK_COUNT;
    if (padPending && comm0_free()) {
        pad_take();
        padLatch |= padNow;
    }
    if (!padPending && !musicInFlight && !musicCmd && comm0_free()) {
        SC_COMM0   = 48;
        padPending = true;
    }
}
unsigned int plat_pad()
{
    if (padPending && comm0_free())
        pad_take();
    const unsigned int latched = padLatch; /* REV R4D */
    padLatch                   = 0;
    if (musicInFlight && comm0_free())
        musicInFlight = false;
    if (musicStopAt && (int)(VBLANK_COUNT - musicStopAt) >= 0) /* REV R4: a track the game does not loop has ended */
        plat_music_stop();
    if (!padPending && !musicInFlight && comm0_free()) {
        if (musicCmd) { /* REV R4: a music command in the request's place this frame */
            SC_COMM2      = musicArg;
            SC_COMM0      = musicCmd;
            musicCmd      = 0;
            musicInFlight = true;
        }
        else {
            unsigned int fx = 0;
            if (sfxQueued) { /* REV R4: the next sound effect in the request's upper byte */
                fx = sfxQueue[0];
                for (int i = 1; i < sfxQueued; ++i) sfxQueue[i - 1] = sfxQueue[i];
                sfxQueued--;
            }
            SC_COMM0   = (unsigned short)(48 | fx << 8);
            padPending = true;
        }
    }
#if SCD_PLANES && SCD_DEVMENU
    if ((padNow & (PAD_A | PAD_B | PAD_C | PAD_START)) == (PAD_A | PAD_B | PAD_C | PAD_START) && Engine.gameMode == ENGINE_MAINGAME)
        Engine.gameMode = ENGINE_INITDEVMENU; /* REV R4: A + B + C + START - back to the dev menu */
#endif
#if SCD_DEMO_PAD
    static unsigned int padHeld = 0; /* the buttons held last frame */
    unsigned int pressed        = padNow & ~padHeld;
    padHeld                     = padNow;
    if (scdDemoPad && Engine.gameMode == ENGINE_MAINGAME) {
        if (!pressed)
            return demo_pad();
        scdDemoPad = 0; /* a button pressed in a stage: the controller from now on (the menu's own presses do not count) */
    }
#endif
    return padNow | latched;
}
#if SCD_PLANES
// REV R1: each drawn layer slot's scroll (DrawHLineScrollLayer) - layer 0 (the foreground) for
// plane A, the first other layer for plane B (two background layers at once: no record)
unsigned short *plat_layer_begin(int slot, int layer, int tileYPos, unsigned short *frame) SCD_HOT; /* R1W: SDRAM */
unsigned short *plat_layer_begin(int slot, int layer, int tileYPos, unsigned short *frame)
{
    if (scdPicture) { /* REV R4C: the 32X picture mode draws the layer (plat_layer_end) */
        if (slot >= 0 && slot < 4)
            pmTileY[slot] = tileYPos;
        return frame;
    }
    if (slot >= 0 && slot < 4) {
        slotUsed[slot]  = true;
        slotLayer[slot] = (unsigned char)layer;
        if (layer == 0)
            tileYA = tileYPos;
        else if (frameBg < 0) {
            frameBg = layer;
            tileYB  = tileYPos;
        }
    }
    return frame;
}
void plat_layer_line(int slot, int line, int x)
{
    if (slot < 0 || slot >= 4 || line < 0 || line >= SCREEN_YSIZE)
        return;
    if (slotLayer[slot] == 0)
        xA[line] = x;
    else if (slotLayer[slot] == frameBg)
        xB[line] = x;
}
void plat_layer_end(int slot)
{
    if (scdPicture)
        pm_layer_end(slot); /* REV R4C */
}
int *plat_layer_lines(int slot) SCD_HOT; /* R1W: SDRAM */
int *plat_layer_lines(int slot) /* R1T: as plat_layer_line - layer 0 into xA, the frame's background layer into xB */
{
    if (scdPicture)
        return pmLines; /* REV R4C: the 32X picture mode's (plat_layer_end) */
    if (slot < 0 || slot >= 4)
        return 0;
    if (slotLayer[slot] == 0)
        return xA;
    if (slotLayer[slot] == frameBg)
        return xB;
    return 0; /* a second background layer: no record this frame (planes_frame) - its lines are not used */
}
// REV R3: an engine sprite draw call -> the act's sprite list (md/md_sprites.c). The draw lists after the stage's
// last front tile layer are in front of it (priority bit); the others behind, as DrawStageGFX draws them.
void plat_sprite(int kind, int sheet, int sx, int sy, int w, int h, int x, int y, int dir, int rot, int px, int py) SCD_HOT;
void plat_sprite(int kind, int sheet, int sx, int sy, int w, int h, int x, int y, int dir, int rot, int px, int py)
{
    if (!planesOn || !sprites.pack)
        return;
    int last = -1;
    for (int t = 3; t >= 0 && t >= tLayerMidPoint; --t)
        if (activeTileLayers[t] < LAYER_COUNT) {
            last = t;
            break;
        }
    static const int after[4] = { 1, 2, 3, 5 }; /* the first draw list after tile slot 0, 1, 2, 3 */
    mds_sprite(&sprites, kind, sheet, sx, sy, w, h, x, y, dir, rot, px, py, last < 0 || scdDrawLayer >= after[last]);
}
#else
void plat_sprite(int, int, int, int, int, int, int, int, int, int, int, int) {}
unsigned short *plat_layer_begin(int, int, int, unsigned short *frame) { return frame; }
void plat_layer_line(int, int, int) {}
void plat_layer_end(int) {}
int *plat_layer_lines(int) { return 0; }
#endif
void plat_log(const char *) {} // S7c: the 32X layer stays blank during normal running (OpenLara); errors only
void plat_fatal(const char *text)
{
    char t[16];
    int i = 0;
    for (; i < 13 && text[i]; ++i) t[i] = text[i];
    t[i] = 0;
    STATE(0x5CE5);
#if SCD_PLANES
    splash_fail(); /* REV R4B: the Genesis text on (line 10) */
    if (planesOn) { /* REV R1: the record area held the planes' records - the box from scratch */
        planesOn    = false;
        statusReady = false;
    }
#endif
    scd_status(1, t, PX_RED);
    for (;;) {
    }
}
bool plat_running() { return true; }

// the stage's console cart image ("SCCB" v2): part k at its header offset
void *plat_cart_data(int kind)
{
    const unsigned char *img = CART_STAGE;
    const unsigned int *h    = (const unsigned int *)img;
    if (h[0] != 0x53434342u || h[1] != 2 || kind < 0 || (unsigned)kind >= h[2])
        plat_fatal("CART IMG BAD");
    return (void *)(img + h[3 + 5 * kind]);
}
// REV R4: the scene's own scripts as native code: the image's part 18, when it has one (engine/Script.cpp binds it if
// it was linked against this engine and made from this bytecode)
const void *plat_native_blob()
{
    const unsigned int *h = (const unsigned int *)CART_STAGE;
    if (h[0] != 0x53434342u || h[1] != 2 || h[2] <= SCD_CART_NATIVE || h[3 + 5 * SCD_CART_NATIVE + 1] == 0)
        return (const void *)0;
    return CART_STAGE + h[3 + 5 * SCD_CART_NATIVE];
}
unsigned int plat_engine_sum() /* FNV-1a over the engine image's 32-bit words (its length: the boot record), once */
{
    static unsigned int sum = 0;
    static bool done        = false;
    if (!done) {
        const unsigned int *p = (const unsigned int *)0x02000000;
        unsigned int n = SCD_BOOT_RECORD[1] / 4, h = 0x811C9DC5u;
        for (unsigned int i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
        sum  = h;
        done = true;
    }
    return sum;
}
// S7d: the scene's cart image. Every scene image has the same part offsets (tools/build_game_data.py)
// - the engine bound them at start-up - so a scene change only replaces the contents at 0x080000.
// The load itself runs in the boot loader's resident routine in SDRAM (Kobo's CMD 46 copies with RV
// set: the SH2 cannot read the cart - where this code runs - meanwhile).
typedef int (*ScdLoadFile)(unsigned short number, unsigned int *bytes);
static unsigned int firstOffsets[32];
static bool haveOffsets = false;
static void scene_stop(unsigned short code, const char *what, unsigned int file)
{
    char t[16], *p = t;
#if SCD_PLANES
    splash_fail(); /* REV R4B: the Genesis text on (line 10) */
#endif
    STATE(code);
    while (*what) *p++ = *what++;
    hex(p, file, 3);
    scd_status(1, t, PX_RED);
    for (;;) {
    }
}
#if SCD_SCENE_CHECK
static uint16_t cart_scene16(uint32_t off) /* S7d2: CartRead16's own types (uint32_t = unsigned long on the SH2) */
{
    if ((off & 0xFFFF) == 0)
        STATE(0x5800 | (off >> 16)); /* R1D: line 10 58nn = the image check is reading at 0x080000 + nn * 64 KB */
    return *(volatile uint16_t *)(0x22080000 + off);
}
#endif
static unsigned int scene_file() /* the disc file of the scene the engine is about to load */
{
    unsigned int file = 0x100 + ((unsigned int)activeStageList << 8) + (unsigned int)stageListPosition + player_files();
#if defined(SCD32X_START_LIST)
    /* R1Q: no double load. The boot stack loads disc file 0x100 ("L0S000.IMG") as the first scene image. On a
       disc that starts in another scene, that file holds the START scene's image instead (game data, see
       docs/R1_NOTES.md), so the start scene IS file 0x100 and the engine finds it already in the cart; the
       title, if it is ever asked for, takes the start scene's number. Boot stack unchanged. */
    {
        unsigned int startFile = 0x100 + ((unsigned int)SCD32X_START_LIST << 8) + (unsigned int)SCD32X_START_STAGE;
        if (file == startFile)
            file = 0x100;
        else if (file == 0x100)
            file = startFile;
    }
#endif
    return file;
}
/* REV R4: a scene change loads only the chunks of the new image that differ from the image in the cart. Every image's
   header has two CRCs for each of its 32 KB chunks ("SCHK" after the part table, tools/build_game_data.py): the SH2
   keeps the cart image's list, loads the new image's chunk 0 (its header), compares, and loads only the chunks whose
   CRCs differ - Sonic's and Tails's Palmtree Panic 1 differ in 7 of 34 besides the header (R4A's images; another zone:
   about 24). The loads run in SDRAM (SCD_HOT): while the
   Sub-CPU's command 46 writes the cart through the 68000, which holds the cart meanwhile, the SH2 must not read the
   cart - neither code nor data - so the chunk list is in SDRAM too. The same commands as the boot loader's resident
   routine (sh2_engboot.c scd_load_file), which loads whole files (the act files still go through it). */
#define SCD_CHUNKS_MAX 96
#define SCHK_MAGIC 0x5343484Bu /* "SCHK" */
/* the cart image's CRCs, kept while chunk 0 is replaced: in the frame buffer's record area (no plane records while a
   scene loads; after the dev menu's two text copies) - 768 bytes of SDRAM spared */
#define oldHash ((volatile unsigned int *)0x24002C00)
static unsigned short loadList[SCD_CHUNKS_MAX];
static int loadCount;
static unsigned int loadLen;
static int sd_cmd(unsigned short cmd) SCD_HOT_NOINLINE; /* COMM0 = cmd, until the Sub-CPU is done; -1: no answer */
static int sd_cmd(unsigned short cmd)
{
    SC_COMM0 = cmd;
    for (unsigned int n = 0; n < 800000u; ++n) {
        if (SC_COMM0 == 0 && SC_COMM0 == 0) /* twice: a read during the 68000's write is undefined */
            return 0;
        for (volatile int k = 0; k < 64; ++k) {
        } /* Kobo HW6: never hammer a COMM register */
    }
    return -1;
}
static int sd_cmd_long(unsigned short cmd) SCD_HOT_NOINLINE; /* REV R4C: a run of chunks - waited for until done (no
                                                                limit: another command while the Sub-CPU works would
                                                                garble both); 0 */
static int sd_cmd_long(unsigned short cmd)
{
    SC_COMM0 = cmd;
    while (SC_COMM0 != 0 || SC_COMM0 != 0) { /* twice: a read during the 68000's write is undefined */
        for (volatile int k = 0; k < 64; ++k) {
        } /* Kobo HW6: never hammer a COMM register */
    }
    return 0;
}
static int sd_open_first(unsigned short number) SCD_HOT_NOINLINE; /* open the file, its chunk 0 into the cart */
static int sd_open_first(unsigned short number)
{
    SC_COMM2 = number;
    if (sd_cmd(38) < 0)
        return -2;
    unsigned int len = SC_COMM8_32;
    if ((int)len <= 0)
        return -1; /* not on the disc: nothing copied */
    loadLen     = len;
    SC_COMM8_32 = len;
    SC_COMM2    = 0;
    SC_COMM12   = 0; /* the Sub-CPU replies 0 = chunk verified */
    if (sd_cmd(46) < 0)
        return -2;
    return SC_COMM12 != 0;
}
/* REV R4C - FAST LOAD (as the boot loader's scd_load_file, cd32x/sh2_engboot.c): the listed chunks go in runs - up to
   SCD_CHUNK_RUN consecutive chunks (a chunk of the old image between two listed ones is taken along: reading 32 KB
   again costs less than another seek) in one Sub-CPU command 46 with COMM2 = first | (more) << 8: one CD read, no
   read-back; its answer, a checksum of what it read (COMM8), is compared with the cart here (the loader's
   scd_cart_sum, SDRAM: boot record [8]). A mismatch: the run again chunk by chunk, the old verified way. Cart code from
   here (REV R4C, not SDRAM): the SH2 must not touch the cart only WHILE a command 46 runs - sd_cmd (SDRAM) waits for it
   to finish, and its caller runs on after that. */
#define SCD_CHUNK_RUN 4 /* the Sub-CPU's CHUNK_RUN_MAX (its 128 KB Word RAM bank) */
typedef unsigned int (*ScdCartSum)(unsigned int at, unsigned int bytes);
static int sd_load_one(unsigned int c) /* the old way: one chunk, verified by the Sub-CPU; -2 no answer, 1 bad */
{
    SC_COMM8_32 = loadLen;
    SC_COMM2    = (unsigned short)c;
    SC_COMM12   = 0;
    if (sd_cmd(46) < 0)
        return -2;
    return SC_COMM12 != 0;
}
static int sd_load_list(unsigned int cartAt) /* the listed chunks of the file at cartAt; the number not verified, -2: no
                                                answer */
{
    int bad                = 0;
    unsigned int sumBase   = SCD_BOOT_RECORD[8]; /* the loader's scd_cart_sum, [9] 'SUM1' (an R4B loader: neither - the
                                                    old way only) */
    ScdCartSum cartSum = (SCD_BOOT_RECORD[9] == 0x53554D31u && sumBase >= 0x06000000u && sumBase < 0x06002000u) ? (ScdCartSum)sumBase
                                                                                                             : (ScdCartSum)0;
    for (int i = 0; i < loadCount;) {
        unsigned int first = loadList[i], last = first;
        int j = i + 1;
        while (j < loadCount && loadList[j] <= last + 2u && loadList[j] < first + SCD_CHUNK_RUN) last = loadList[j++];
        bool ok = false;
        if (last > first && cartSum) {
            unsigned int from = first << 15, to = (last + 1) << 15;
            if (to > loadLen)
                to = loadLen;
            SC_COMM8_32 = loadLen;
            SC_COMM2    = (unsigned short)(first | (last - first) << 8);
            SC_COMM12   = 0xFFFF;
            if (sd_cmd_long(46) < 0)
                return -2;
            ok = SC_COMM12 == 0 && SC_COMM8_32 == cartSum(cartAt + from, (to - from + 2047u) & ~2047u);
        }
        for (unsigned int c = first; !ok && c <= last; ++c) {
            int r = sd_load_one(c);
            if (r < 0)
                return -2;
            bad += r;
        }
        i = j;
        STATE(0x5D00 | (last & 0xFF)); /* as the boot loader: 5Dnn = chunk nn */
#if SCD_PLANES
        splash_chunk((unsigned int)i, (unsigned int)loadCount); /* REV R4B: the loading bar (cart code: the cart is free) */
#endif
    }
    return bad;
}
static int big_file(unsigned short number, unsigned int at); /* REV R4G (THE PRELOAD, below) */
static int sd_load_whole(unsigned short number, unsigned int *bytes, unsigned int at) /* REV R4B: a whole file into its cart
                                                                                         place, as the boot loader's routine
                                                                                         (scd_load_file) - with the bar */
{
    if (at != 0x380000u) { /* REV R4G: an act file into a slot of the 16 MB cart's page 7 */
        const int r = big_file(number, at);
        *bytes      = loadLen;
        return r;
    }
    int r = sd_open_first(number);
    if (r == -1)
        return -1; /* not on the disc */
    if (r < 0)
        return 1; /* no answer */
    unsigned int total = (loadLen + 32767u) >> 15;
    loadCount          = 0;
    for (unsigned int c = 1; c < total && c < SCD_CHUNKS_MAX; ++c) loadList[loadCount++] = (unsigned short)c;
    int bad = sd_load_list(0x380000u); /* (act files only: SGB_CART_OFFSET) */
    *bytes  = loadLen;
    return r + (bad < 0 ? 1 : bad);
}
/* REV R4E/R4F: MicroNut99's 8 / 16 MB cart - two 2 MB windows (window 0 = cart 0x000000-0x1FFFFF, window 1 =
   0x200000-0x3FFFFF), each showing one of 8 pages of 2 MB (an 8 MB cart: pages 4-7 are 0-3 again). The Sub-CPU switches
   a window with command 46, COMM2 = 0xF800 | window << 8 | page (the 68000 writes the page register with RV = 1 and
   interrupts off); this SH2 purges its cache after each switch (the second SH2 purges before each job). Command 46
   with COMM2 = 0xF000 | slot puts the file command 38 opened into that 128 KB slot instead of its own. */
#define CART_W1 0x200000u
#define CART_PROBE 0x10000u      /* the engine's code: page 0 at this place is not a run of equal words */
#define CART_PROBE_IMG 0x080000u /* REV R4G: the scene image's header - the act pages hold copies of the engine, so a
                                    page shows page 0 (an alias) only if both places are page 0's */
static int sd_page(int window, int page)
{
    SC_COMM2  = (unsigned short)(0xF800u | (unsigned)window << 8 | ((unsigned)page & 7u));
    SC_COMM12 = 0xFFFF;
    const int r = sd_cmd(46);
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: a window shows other RAM */
    return r;
}
static bool cart_same(unsigned int a, unsigned int b, unsigned int bytes) /* two cart places, uncached */
{
    const volatile unsigned int *p = (const volatile unsigned int *)(0x22000000u + a);
    const volatile unsigned int *q = (const volatile unsigned int *)(0x22000000u + b);
    for (unsigned int i = 0; i < bytes / 4; ++i)
        if (p[i] != q[i])
            return false;
    return true;
}
static bool w1_is_page0() /* window 1 shows what window 0 shows (page 0): the same engine and the same scene image */
{
    return cart_same(CART_PROBE, CART_W1 + CART_PROBE, 0x1000) && cart_same(CART_PROBE_IMG, CART_W1 + CART_PROBE_IMG, 0x1000);
}
static int sd_slot(unsigned int slot) /* the opened file's cart slot (128 KB each): the Sub-CPU's command 46 0xF0nn */
{
    SC_COMM2  = (unsigned short)(0xF000u | (slot & 0x1Fu));
    SC_COMM12 = 0xFFFF;
    return sd_cmd(46);
}
#if SCD_PLANES
/* REV R4G: THE PRELOAD - MicroNut99's 16 MB cart is battery-backed: what its pages hold stays through power-off. The
   game keeps the act being played in it, every time period, and the next act's start:
   - pages 0 and 1: the cart at power-on, as the 4 MB game uses it - the engine, the boot loader's title, the menus,
     the special stages, the title's demos (the scene image at 0x080000 and up into page 1), the data pack with the
     loading picture (0x300000) and an act file slot (0x380000);
   - pages 2-6: the ACT PAGES - each the engine (its own copy, the same as the running one byte for byte, so window 0
     can show it under the running code) and an act's scene image at 0x080000, used where it lies;
   - page 7: the act files of act pages 2-5, a 512 KB slot each (cart 0x200000, 0x280000, 0x300000, 0x380000 in window
     1); page 6's goes into page 1's slot (0x380000, shared with the menus' and the credits' files).
   A scene in an act page: window 0 to that page, window 1 to its act file's page - two page switches, no disc. At an
   act's start (its first scene, not a title demo), the act's other periods and the next act's start (after act 1 act
   2's present, after act 2 act 3's bad future, after act 3 the next zone's act 1) are loaded into the other act
   pages, the music off (a CD data read stops it), the loading picture and its bar up. After a power-on nothing in
   SDRAM says what the pages hold: the first act scene looks each page and slot up in the preload table
   (tools/build_preload.py: in SCDDATA.PAK, which the boot loader brings in at every power-on) by its header's
   fingerprint, then checks it whole with the loader's checksum over its length - a page half loaded when the power
   went off does not match and is loaded again. The 8 MB cart: one act page (2), its act file in page 3. The 4 MB cart,
   or BIG CART off: none of this (bigN 0). Line 10: 5CB1 the pages looked up, 5CB2 an engine copy loaded, 5CB3 a page
   could not be made ready (the scene in page 0, as the 4 MB game), 5CB4 a preload. */
/* REV R4I: the menu keeps its own pages, and four act pages remain:
   - pages 2-5: the act pages (the act's present, past, the next act's start, the bad future - the good future, the
     rarest, from the disc when warped to);
   - page 6: the menu (L0S001, the save screen - 2.2 MB, more than a page) seen in window 0 with its own copy of the
     engine, page 7 in window 1 its last 0x98000 bytes (cart 0x200000-0x297FFF);
   - the act file slots: 1 and 2 in page 7 (cart 0x300000, 0x380000 - below the menu's tail), 3 in page 1 at
     0x260000 (after the title's image; a bigger scene shown from page 0 - a special stage, the help, the secrets - runs
     over it: its act file is then loaded again), 0 in page 1 at 0x380000 (shared with the Options Menu's font, the
     credits' and the demos' act files).
   After a power-on the menu's pages are looked up as the act pages are (its header, then the checksum over both). */
#define BIG_MAX 4
#define BIG_MENU BIG_MAX /* (the menu's index in bigEng / bigScene: page 6) */
#define BIG_MENU_FILE 0x101u /* (L0S001.IMG; not the Options Menu's MENU_FILE above) */
static unsigned char bigN    = 0;     /* act pages: 4 (16 MB), 1 (8 MB), 0 (none) */
static signed char bigNow    = -1;    /* the act page window 0 shows (-1: page 0) */
static unsigned char bigW1   = 1;     /* the page window 1 shows */
static unsigned char bigSlot = 0;     /* the act file slot window 1 is set for (sgbOff, mdgInCart) */
static bool bigKnown         = false; /* the act pages and slots looked up (after a power-on: not yet) */
static unsigned char bigEng[BIG_MAX + 1];    /* the act page's engine copy checked this session (+ the menu's page) */
static unsigned short bigScene[BIG_MAX + 1]; /* the scene image in each act page (0: none, or not whole) (+ the menu) */
static unsigned short bigTodo[BIG_MAX];  /* the scene to preload into it */
static unsigned short slotMdg[BIG_MAX];  /* the act file in each slot (the slot window 1 is set for: mdgInCart) */
static unsigned int bigHome  = 0;        /* page 0's scene while an act page is shown */
static unsigned int bigGroup = 0;        /* the act preloaded last (scene_group) */
static int big_page(int i) { return 2 + i; }
static int big_slot_of(int i) { return bigN == 1 ? 1 : i < 3 ? i + 1 : 0; }
static int big_slot_page(int s) { return s == 0 || s == 3 ? 1 : bigN == 1 ? 3 : 7; }
static unsigned int big_slot_off(int s)
{
    return s == 0 ? 0x380000u : bigN == 1 ? CART_W1 : s == 1 ? 0x300000u : s == 2 ? 0x380000u : 0x260000u;
}
static unsigned int big_scene_of(int i) { return i == bigNow ? SCD_BOOT_RECORD[5] : bigScene[i]; }
static unsigned int big_slot_mdg(int s) { return s == bigSlot ? mdgInCart : slotMdg[s]; }
static void big_w1(int page)
{
    if (page != bigW1) {
        sd_page(1, page);
        bigW1 = (unsigned char)page;
    }
}
static void big_use_slot(int s) /* window 1 to slot s's page; sgbOff and mdgInCart for that slot */
{
    big_w1(big_slot_page(s));
    if (s != bigSlot) {
        slotMdg[bigSlot] = (unsigned short)mdgInCart;
        mdgInCart        = slotMdg[s];
        bigSlot          = (unsigned char)s;
    }
    sgbOff = big_slot_off(s);
}
static void big_home_w1() /* (planes_off) window 1 to page 1: the loading picture, the menus' act file slot */
{
    if (bigN) {
        comm_idle();
        big_use_slot(0);
    }
}
static void big_show(int i) /* window 0 to act page i (-1: page 0; BIG_MENU: the menu's), window 1 to its act file's page
                               (the menu: page 7, its image's tail) */
{
    comm_idle();
    if (i == BIG_MENU)
        big_w1(7); /* (the act file slots' bookkeeping unchanged: the menu has no act file) */
    else
        big_use_slot(i < 0 ? 0 : big_slot_of(i));
    if (i != bigNow) {
        if (bigNow < 0)
            bigHome = SCD_BOOT_RECORD[5];
        else
            bigScene[bigNow] = (unsigned short)SCD_BOOT_RECORD[5];
        sd_page(0, i < 0 ? 0 : big_page(i)); /* the same engine in both pages (big_engine): the code runs on */
        SCD_BOOT_RECORD[5] = i < 0 ? bigHome : bigScene[i];
        bigNow             = (signed char)i;
    }
}
static int big_file(unsigned short number, unsigned int at) /* a whole file to cart `at` (a slot's start, window 1): 0 =
                                                               in, -1 = not on the disc, else the chunks not verified */
{
    SC_COMM2 = number;
    if (sd_cmd(38) < 0)
        return 1;
    const unsigned int len = SC_COMM8_32;
    if ((int)len <= 0)
        return -1;
    if (len > 0x80000u)
        return 1; /* more than its 512 KB slot: not loaded */
    if (sd_slot(at >> 17) < 0)
        return 1;
    loadLen            = len;
    unsigned int total = (len + 32767u) >> 15;
    loadCount          = 0;
    for (unsigned int c = 0; c < total && c < SCD_CHUNKS_MAX; ++c) loadList[loadCount++] = (unsigned short)c;
    const int bad = sd_load_list(at);
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    return bad < 0 ? 1 : bad;
}
static bool big_engine(int i) /* act page i's copy of the engine: the running one's, byte for byte (loaded if not) */
{
    if (bigEng[i])
        return true;
    const unsigned int len = (SCD_BOOT_RECORD[1] + 3u) & ~3u; /* the engine's length (the boot loader's record) */
    if (len == 0 || len > 0x80000u)
        return false;
    comm_idle();
    big_w1(big_page(i));
    bool ok = cart_same(0, CART_W1, len);
    if (!ok) {
        STATE(0x5CB2);
        ok = big_file(21, CART_W1) == 0 && cart_same(0, CART_W1, len); /* SONICCD.PAK */
    }
    bigEng[i] = ok ? 1 : 0;
    return ok;
}
struct BigEntry { /* tools/build_preload.py: the preload table in SCDDATA.PAK (16 bytes, big-endian) */
    unsigned short file, kind;
    unsigned int len, sum, fp;
};
static const BigEntry *big_table(unsigned int *n) /* (window 1: page 1) */
{
    const unsigned int sz = SCD_BOOT_RECORD[2]; /* SCDDATA.PAK's length */
    if (sz < 48 || sz > 0x80000u)
        return 0;
    const unsigned char *t = CART_PACK + sz - 32;
    const unsigned int off = be32(t + 4), cnt = be32(t + 8);
    if (be32(t) != 0x50524531u || (off & 3) || cnt > 1024 || off + cnt * 16u > sz - 32) /* 'PRE1' */
        return 0;
    *n = cnt;
    return (const BigEntry *)(CART_PACK + off);
}
static unsigned int big_head_fp(unsigned int at, int kind) /* the fingerprint of the head of what is at cart `at`: an
                                                              image's header to the end of its chunk list, an act file's
                                                              first 4 KB - FNV-1a over 32-bit words (0: not one) */
{
    const unsigned int *h = (const unsigned int *)(0x02000000u + at);
    unsigned int words    = 1024;
    if (kind == 1) {
        const unsigned int np = h[2];
        if (h[0] != 0x53434342u || h[1] != 2 || np == 0 || np >= 64 || h[3 + 5 * np] != SCHK_MAGIC || h[4 + 5 * np] > SCD_CHUNKS_MAX)
            return 0;
        words = 3 + 5 * np + 2 + 2 * h[4 + 5 * np];
    }
    else if (h[0] != SGB_MAGIC)
        return 0;
    unsigned int f = 0x811C9DC5u;
    for (unsigned int k = 0; k < words; ++k) f = (f ^ h[k]) * 0x01000193u;
    return f;
}
static void sum_part(unsigned int at, unsigned int bytes, unsigned int *a, unsigned int *b) /* REV R4I: the loader's
                                                                                        checksum (scd_cart_sum) in
                                                                                        parts: its running sums go on */
{
    const unsigned int *p = (const unsigned int *)(0x02000000u + at); /* cached: the cache purged by the page switch */
    unsigned int x = *a, y = *b;
    for (unsigned int n = bytes >> 2; n--;) {
        x += *p++;
        y += x;
    }
    *a = x, *b = y;
}
static void big_identify() /* what each act page and slot holds, whole - after a power-on (or the cart test) */
{
    STATE(0x5CB1);
    comm_idle();
    const int slots = bigN == 1 ? 2 : BIG_MAX;
    unsigned int fp[2 * BIG_MAX], len[2 * BIG_MAX], sum[2 * BIG_MAX];
    unsigned short file[2 * BIG_MAX];
    for (int k = 0; k < 2 * BIG_MAX; ++k) fp[k] = 0, file[k] = 0;
    for (int i = 0; i < bigN; ++i) { /* 1. the heads */
        big_w1(big_page(i));
        fp[i] = big_head_fp(CART_W1 + 0x080000u, 1);
    }
    for (int s = 0; s < slots; ++s) {
        big_w1(big_slot_page(s));
        fp[BIG_MAX + s] = big_head_fp(big_slot_off(s), 2);
    }
    big_w1(1); /* 2. looked up in the table */
    unsigned int n = 0;
    const BigEntry *t = big_table(&n);
    for (int k = 0; t && k < 2 * BIG_MAX; ++k)
        for (unsigned int j = 0; fp[k] && j < n; ++j)
            if (t[j].fp == fp[k] && t[j].kind == (k < BIG_MAX ? 1 : 2)) {
                file[k] = t[j].file, len[k] = t[j].len, sum[k] = t[j].sum;
                break;
            }
    unsigned int sumBase = SCD_BOOT_RECORD[8]; /* 3. whole: the loader's checksum over the length */
    ScdCartSum cartSum   = (SCD_BOOT_RECORD[9] == 0x53554D31u && sumBase >= 0x06000000u && sumBase < 0x06002000u) ? (ScdCartSum)sumBase : (ScdCartSum)0;
    for (int i = 0; i < bigN; ++i) {
        bigScene[i] = 0;
        if (file[i] && cartSum) {
            big_w1(big_page(i));
            if (cartSum(CART_W1 + 0x080000u, (len[i] + 2047u) & ~2047u) == sum[i])
                bigScene[i] = file[i];
        }
    }
    for (int s = 0; s < slots; ++s) {
        unsigned short got = 0;
        if (file[BIG_MAX + s] && cartSum) {
            big_w1(big_slot_page(s));
            if (cartSum(big_slot_off(s), (len[BIG_MAX + s] + 2047u) & ~2047u) == sum[BIG_MAX + s])
                got = file[BIG_MAX + s];
        }
        if (s == bigSlot)
            mdgInCart = got;
        else
            slotMdg[s] = got;
    }
    bigScene[BIG_MENU] = 0; /* REV R4I: the menu - its header in page 6, its checksum over pages 6 and 7 */
    if (bigN == BIG_MAX && cartSum) {
        big_w1(6);
        const unsigned int f6 = big_head_fp(CART_W1 + 0x080000u, 1);
        unsigned int mlen = 0, msum = 0;
        big_w1(1); /* (the table is in page 1) */
        for (unsigned int j = 0; f6 && t && j < n; ++j)
            if (t[j].fp == f6 && t[j].kind == 1 && t[j].file == BIG_MENU_FILE) {
                mlen = t[j].len, msum = t[j].sum;
                break;
            }
        const unsigned int bytes = (mlen + 2047u) & ~2047u, head = 0x200000u - 0x080000u;
        if (mlen > head && bytes - head <= 0x0F0000u) {
            unsigned int a = 0, b = 0;
            big_w1(6);
            sum_part(CART_W1 + 0x080000u, head, &a, &b); /* page 6: 0x080000-0x1FFFFF */
            big_w1(7);
            sum_part(CART_W1, bytes - head, &a, &b); /* page 7: the rest */
            if ((a ^ (b << 13 | b >> 19)) == msum)
                bigScene[BIG_MENU] = (unsigned short)BIG_MENU_FILE;
        }
    }
    big_use_slot(bigSlot); /* window 1 as it was */
    bigKnown = true;
}
static int big_targets(unsigned int file, unsigned short *t) /* the act's scenes and the next act's start - `file` first,
                                                                 then the most needed: the present, the past, the next
                                                                 act's start, the bad future, the good future (the last
                                                                 one gets the act page whose act file shares page 1's slot
                                                                 with the menus and the title demos) */
{
    const unsigned int pl = file & 0x80u, pos = file & 0x7Fu, z = pos / 10u, a = pos % 10u, base = z * 10u + (a < 4 ? 0u : 4u);
    const unsigned int next = 0x200u | pl | (a < 4 ? base + 4u : a < 8 ? z * 10u + 9u : (z + 1u) * 10u); /* act 2's present,
                                                                                   act 3's bad future (the usual one), the
                                                                                   next zone's act 1 */
    unsigned int order[5];
    int m = 0;
    if (a < 8) {
        order[m++] = 0x200u | pl | base;        /* present */
        order[m++] = 0x200u | pl | (base + 1u); /* past */
        order[m++] = next;
        order[m++] = 0x200u | pl | (base + 3u); /* bad future */
        order[m++] = 0x200u | pl | (base + 2u); /* good future */
    }
    else {
        order[m++] = 0x200u | pl | (z * 10u + (a == 8 ? 9u : 8u)); /* act 3's other future */
        if (z < 6)
            order[m++] = next;
    }
    int n  = 0;
    t[n++] = (unsigned short)file;
    for (int k = 0; k < m; ++k)
        if (order[k] != file)
            t[n++] = (unsigned short)order[k];
    return n < bigN ? n : bigN;
}
static bool big_attract() { return GetGlobalVariableByName("Options.AttractMode") != 0; } /* a title demo plays */
static unsigned int scene_group(unsigned int file) /* an act's time periods share it (0: not an act scene) */
{
    if ((file & 0xF00u) != 0x200u)
        return 0;
    const unsigned int pos = file & 0x7Fu, a = pos % 10u; /* each zone: act 1 A-D, act 2 A-D, act 3 C-D */
    return 0x10000u | (file & 0x80u) | (pos / 10u) << 2 | (a < 4 ? 0u : a < 8 ? 1u : 2u);
}
static void big_prepare(unsigned int file) /* before a scene loads: window 0 to the act page it is in or goes to (the
                                              other scenes: page 0); an act's start plans its preload */
{
    if (!bigN)
        return;
    const unsigned int g = scene_group(file);
    barImgTo = barMdgFrom = 20;
    const bool attract = big_attract();
    if (file == BIG_MENU_FILE && bigN == BIG_MAX && !attract) { /* REV R4I: the menu in its own pages (6 and 7) */
        if (!bigKnown)
            big_identify();
        splash_part(0, 0);
        if (big_engine(BIG_MENU)) {
            big_show(BIG_MENU);
            return;
        }
        STATE(0x5CB3);
    }
    if (g && attract && bigKnown) /* REV R4I: a title demo of a scene an act page holds: shown from there */
        for (int i = 0; i < bigN; ++i)
            if (big_scene_of(i) == file && bigEng[i]) {
                big_show(i);
                return;
            }
    if (!g || attract) {
        big_show(-1);
        return;
    }
    if (!bigKnown)
        big_identify();
    unsigned short t[BIG_MAX];
    const int n      = big_targets(file, t);
    const bool start = g != bigGroup;
    int at[BIG_MAX];
    unsigned int used = 0;
    for (int i = 0; i < bigN; ++i) bigTodo[i] = 0;
    for (int j = 0; j < n; ++j) {
        at[j] = -1;
        for (int i = 0; i < bigN && at[j] < 0; ++i)
            if (!(used >> i & 1) && big_scene_of(i) == t[j]) {
                at[j] = i;
                used |= 1u << i;
            }
    }
    int pending = 0;
    for (int j = 0; j < n; ++j)
        if (at[j] < 0 && (j == 0 || start)) { /* a page for it: one holding none of these (page 1's act file slot the last) */
            int i = 0;
            while (i < bigN && ((used >> i & 1) || big_slot_of(i) == 0)) ++i;
            if (i >= bigN)
                for (i = 0; i < bigN && (used >> i & 1); ++i) {
                }
            if (i >= bigN)
                break;
            if (!start && big_scene_of(i) && scene_group(big_scene_of(i)) == g)
                break; /* (8 MB: one act page) it holds this act's start: it stays, this period loads in page 0 as on the
                          4 MB cart */
            at[j] = i;
            used |= 1u << i;
            if (j) {
                bigTodo[i] = t[j];
                ++pending;
            }
        }
    bigGroup = g;
    if (pending)
        barImgTo = 4, barMdgFrom = 28; /* the bar: the scene 0-4, the preload 4-28, its act file 28-32 */
    splash_part(0, 0);                 /* (an engine copy loaded now: no share of the bar) */
    if (at[0] < 0 || !big_engine(at[0])) {
        if (at[0] >= 0)
            STATE(0x5CB3); /* the act page not usable: the scene in page 0, as the 4 MB game */
        for (int i = 0; i < bigN; ++i) bigTodo[i] = 0;
        barImgTo = barMdgFrom = 20;
        big_show(-1);
        return;
    }
    big_show(at[0]);
}
static int scene_load_at(unsigned int file, unsigned int base, int barFrom, int barTo, bool trust);
static void big_preload() /* (plat_stage_loading, the scene in) the act's other periods and the next act's start */
{
    if (!bigN || bigNow < 0)
        return;
    int pending = 0, k = 0;
    for (int i = 0; i < bigN; ++i)
        if (bigTodo[i])
            ++pending;
    if (!pending)
        return;
    big_w1(1); /* REV R5C: the loading picture (page 1) - up, if the scene itself needed no loading - and the notice */
    if (!splashUp)
        splash_show();
    splash_note("LOADING NEXT LEVEL INTO CART");
    const int cur = bigNow;
    for (int i = 0; i < bigN; ++i) {
        const unsigned int f = bigTodo[i];
        if (!f)
            continue;
        bigTodo[i]  = 0;
        const int a = barImgTo + (barMdgFrom - barImgTo) * k / pending, b = barImgTo + (barMdgFrom - barImgTo) * (k + 1) / pending;
        const int m = (a + b) / 2;
        ++k;
        STATE(0x5CB4);
        splash_part(a, a);
        if (!big_engine(i))
            continue;
        big_w1(big_page(i));
        const bool whole = bigScene[i] != 0; /* (what the page holds is whole: its header's chunk list tells the truth) */
        bigScene[i]      = 0;
        if (scene_load_at(f, CART_W1 + 0x080000u, a, m, whole) == 0)
            bigScene[i] = (unsigned short)f;
        const int s = big_slot_of(i);
        if (bigScene[i] && big_slot_mdg(s) != f + 0x1000u) { /* its act file */
            big_w1(big_slot_page(s));
            splash_part(m, b);
            const unsigned short got = big_file((unsigned short)(f + 0x1000u), big_slot_off(s)) == 0 ? (unsigned short)(f + 0x1000u) : 0;
            if (s == bigSlot)
                mdgInCart = got;
            else
                slotMdg[s] = got;
        }
    }
    big_use_slot(big_slot_of(cur)); /* window 1: the scene's own act file slot again */
}
/* REV R4L: THE BOOT PRELOAD - at power-on, before the title starts (the 16 MB cart): the pages are looked up, and what a
   new game needs and the cart does not hold whole is loaded now, under the loading picture:
   - the menu / save screen (pages 6 and 7, with its engine copy);
   - Palmtree Panic 1's act set (its present, past, act 2's start, its bad future, with their act files and engine
     copies) - only when no act page holds an act scene at all (the first power-on, a new build's first power-on, after
     the cart test). An act kept from the last time played stays: it is what the save goes on with.
   Then the title, the menu and a new game's first act are page switches. A save of another act loads its set at its
   start, as before. Line 10: 5CB5 the boot preload running. */
static void big_boot_preload()
{
    if (bigN != BIG_MAX || big_attract())
        return;
    if (!bigKnown)
        big_identify();
    const bool menuIn = big_scene_of(BIG_MENU) == BIG_MENU_FILE;
    bool actIn        = false;
    for (int i = 0; i < bigN; ++i)
        if (scene_group(big_scene_of(i)))
            actIn = true;
    if (menuIn && actIn) { /* all in: only the engine copies checked (a page whose copy differs gets the engine now) */
        big_engine(BIG_MENU);
        for (int i = 0; i < bigN; ++i)
            if (big_scene_of(i))
                big_engine(i);
        big_home_w1();
        return;
    }
    STATE(0x5CB5);
    if (!splashUp)
        splash_show(); /* (window 1 on page 1: big_detect / big_identify leave it there) */
    splash_note("THIS TAKES A FEW MINUTES", "FIRST TIME  SETTING UP THE CART"); /* REV R5D (the owner's request) */
    const int menuTo = menuIn ? 0 : actIn ? 32 : 8; /* the bar: the menu 0 - 8, the act set 8 - 32 */
    if (!menuIn) {
        splash_part(0, 0);
        if (big_engine(BIG_MENU)) {
            big_show(BIG_MENU);
            if (scene_load_at(BIG_MENU_FILE, 0x080000u, 0, menuTo, false) != 0)
                SCD_BOOT_RECORD[5] = 0; /* (not whole: the menu loads again on its first trip) */
            big_show(-1); /* page 0 again: the title (bigScene[BIG_MENU] = what was loaded) */
        }
    }
    if (!actIn) {
        unsigned short t[BIG_MAX];
        const int n = big_targets(0x200u, t); /* L1S000: Palmtree Panic 1, the present (Sonic's files) */
        unsigned int used = 0;
        for (int j = 0; j < n; ++j) {
            int i = 0; /* a page as big_prepare picks it: page 1's act file slot the last */
            while (i < bigN && ((used >> i & 1) || big_slot_of(i) == 0)) ++i;
            if (i >= bigN)
                for (i = 0; i < bigN && (used >> i & 1); ++i) {
                }
            if (i >= bigN)
                break;
            used |= 1u << i;
            const unsigned int f = t[j];
            const int a = menuTo + (32 - menuTo) * j / n, b = menuTo + (32 - menuTo) * (j + 1) / n, m = (a + b) / 2;
            splash_part(a, a);
            if (!big_engine(i))
                continue;
            big_w1(big_page(i));
            bigScene[i] = 0;
            if (scene_load_at(f, CART_W1 + 0x080000u, a, m, false) == 0)
                bigScene[i] = (unsigned short)f;
            const int s = big_slot_of(i);
            if (bigScene[i] && big_slot_mdg(s) != f + 0x1000u) { /* its act file */
                big_w1(big_slot_page(s));
                splash_part(m, b);
                const unsigned short got = big_file((unsigned short)(f + 0x1000u), big_slot_off(s)) == 0 ? (unsigned short)(f + 0x1000u) : 0;
                if (s == bigSlot)
                    mdgInCart = got;
                else
                    slotMdg[s] = got;
            }
        }
        bigGroup = scene_group(0x200u);
    }
    big_home_w1(); /* window 1: page 1, the title's act file slot */
}
static void big_detect() /* (the first scene load after power-on) MicroNut99's page mapper, 16 or 8 MB - read only */
{
    comm_idle();
    if (sd_page(1, 0) < 0)
        return;
    int mb = 0;
    if (w1_is_page0()) { /* window 1 shows page 0: a mapper */
        sd_page(1, 4);
        mb = w1_is_page0() ? 8 : 16; /* page 4 is page 0 again: 8 MB */
    }
    sd_page(1, 1);
    bigW1 = 1;
    bigN  = mb == 16 ? BIG_MAX : mb == 8 ? 1 : 0;
}
static void big_forget() /* (the cart test wrote into pages 2-7) look the pages up again */
{
    for (int i = 0; i < BIG_MAX; ++i) bigEng[i] = 0, bigTodo[i] = 0;
    bigEng[BIG_MENU] = 0;
    bigKnown = false;
    bigGroup = 0;
}
static bool scene_resident(unsigned int file, unsigned int mdg) /* the scene and its act file in the cart already */
{
    if (file == SCD_BOOT_RECORD[5] && (mdg == mdgInCart || (bigN && !scene_group(file))))
        return true; /* (REV R4I: a scene without an act file - the title, the menus - needs only its image) */
    if (file == BIG_MENU_FILE && bigN == BIG_MAX && bigKnown && !big_attract())
        return big_scene_of(BIG_MENU) == file && bigEng[BIG_MENU]; /* REV R4I: the menu in its pages */
    if (!bigN || !bigKnown || scene_group(file) != bigGroup || !bigGroup || big_attract())
        return false; /* (an act's start: the loading picture - a preload may follow) */
    for (int i = 0; i < bigN; ++i)
        if (big_scene_of(i) == file)
            return big_slot_mdg(big_slot_of(i)) == mdg;
    return false;
}
static void big_home_spill(unsigned int len) /* REV R4I: a scene of `len` bytes loaded into page 0: one longer than the
                                                title runs over act file slot 3 (page 1, 0x260000) */
{
    if (bigN == BIG_MAX && bigNow < 0 && 0x080000u + len > 0x260000u) {
        if (bigSlot == 3)
            mdgInCart = 0;
        else
            slotMdg[3] = 0;
    }
}
int plat_big_cart(int set) /* the Options Menu's BIG CART: -1 asks; 0 off; 8 or 16 on (after the cart test) - 16, 8, 0 */
{
    if (set >= 0) {
        if (bigN)
            big_show(-1); /* page 0 and page 1 again */
        bigN     = set == 16 ? BIG_MAX : set == 8 ? 1 : 0;
        bigKnown = false;
        bigGroup = 0;
    }
    return bigN == BIG_MAX ? 16 : bigN == 1 ? 8 : 0;
}
#else
static void big_home_w1() {}
static void big_prepare(unsigned int) {}
static void big_preload() {}
static void big_detect() {}
static void big_boot_preload() {}
static bool scene_resident(unsigned int, unsigned int) { return false; }
static void big_home_spill(unsigned int) {}
int plat_big_cart(int) { return 0; }
#endif
static int scene_load_at(unsigned int file, unsigned int base, int barFrom, int barTo, bool trust) /* the scene's image into
                                                                                                      the cart at `base`
                                                                                                      (0x080000: window 0's
                                                                                                      page; REV R4G:
                                                                                                      0x280000, an act page
                                                                                                      through window 1): 0 =
                                                                                                      there, -1 = not on the
                                                                                                      disc */
{
    const bool here = base == 0x080000u;
    if (here && file == SCD_BOOT_RECORD[5])
        return 0; /* in the cart already */
    comm_idle(); /* REV R4: the pad request answered first */
    /* 1. the cart image's chunk CRCs, before chunk 0 is replaced (uncached: 0x22...) - REV R4G: only if the image there
       is whole (an act page loaded halfway when the power went off: its header says more than it holds) */
    const volatile unsigned int *h = (const volatile unsigned int *)(0x22000000u + base);
    unsigned int np = h[2], oldN = 0;
    if (trust && h[0] == 0x53434342u && h[1] == 2 && np > 0 && np < 64 && h[3 + 5 * np] == SCHK_MAGIC && h[4 + 5 * np] <= SCD_CHUNKS_MAX) {
        oldN = h[4 + 5 * np];
        for (unsigned int i = 0; i < 2 * oldN; ++i) oldHash[i] = h[5 + 5 * np + i];
    }
    /* 2. the new image's header */
    int r;
    if (here)
        r = sd_open_first((unsigned short)file);
    else { /* REV R4G: chunk 0 into the act page's place (the slot command after command 38) */
        SC_COMM2 = (unsigned short)file;
        r        = sd_cmd(38) < 0 ? -2 : (int)SC_COMM8_32 <= 0 ? -1 : 0;
        if (r == 0) {
            loadLen = SC_COMM8_32;
            if (loadLen > 0x180000u)
                return 1; /* more than an act page holds (0x080000-0x1FFFFF): not loaded */
            SC_COMM8_32 = loadLen;
            r           = sd_slot(base >> 17) < 0 ? -2 : 0;
            SC_COMM2    = 0;
            SC_COMM12   = 0;
            r           = r < 0 || sd_cmd(46) < 0 ? -2 : SC_COMM12 != 0;
        }
    }
    if (r == -1)
        return -1; /* the Sub-CPU found no such file: nothing was copied, the cart holds the last image */
    if (r != 0) {
        if (!here)
            return 1;
        scene_stop(0x5CE2, "BAD LOAD ", file);
    }
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    /* 3. the chunks whose CRCs differ (all of them if either header has no list) */
    unsigned int total = (loadLen + 32767u) >> 15, newN = 0;
    np = h[2];
    if (np > 0 && np < 64 && h[3 + 5 * np] == SCHK_MAGIC && h[4 + 5 * np] == total)
        newN = total;
    loadCount = 0;
    for (unsigned int c = 1; c < total && c < SCD_CHUNKS_MAX; ++c) {
        bool same = c < newN && c < oldN && h[5 + 5 * np + 2 * c] == oldHash[2 * c] && h[6 + 5 * np + 2 * c] == oldHash[2 * c + 1];
        if (!same)
            loadList[loadCount++] = (unsigned short)c;
    }
    STATE(0x5E00 | (loadCount & 0xFF)); /* REV R4: line 10 5Enn = nn chunks to load */
#if SCD_PLANES
    splash_part(barFrom, barTo); /* REV R4B: the loading bar's first part (the act file fills the rest) */
#else
    (void)barFrom, (void)barTo;
#endif
    int bad = sd_load_list(base); /* (the scene image's place: CART_STAGE, or an act page's) */
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    if (bad != 0) {
        if (!here)
            return 1;
        scene_stop(0x5CE2, "BAD LOAD ", file);
    }
    if (!here) { /* REV R4G: a preloaded act page - the fixed layout, or not usable */
        for (int k = 0; k < (int)h[2] && k < 32; ++k)
            if (h[3 + 5 * k] != firstOffsets[k])
                return 1;
        return 0;
    }
    SCD_BOOT_RECORD[5] = file;
    big_home_spill(loadLen); /* REV R4I */
    STATE(0x5C14); /* R1C: the scene is in the cart */
#if SCD_SCENE_CHECK
    CartCheck r;
    if (cart_verify(cart_scene16, &r) != 0)
        scene_stop(0x5CE3, "BAD IMAGE ", file);
#endif
    /* R1D: as OpenJazz REV's level packs (port/plat_mars.c plat_load_pack): no check of the image
       from the engine. The Sub-CPU has read every chunk back already ("CART I4 Cnn ... OK"); this
       whole-image check, never run from the cart before, is where R1C's SH2 died (5C14 -> 101).
       SCD_SCENE_CHECK=1 puts it back (line 10 58nn while it reads). */
    STATE(0x5C15); /* R1C: its image checked (R1D: not checked) */
    for (int k = 0; k < (int)h[2] && k < 32; ++k)
        if (h[3 + 5 * k] != firstOffsets[k])
            scene_stop(0x5CE9, "LAYOUT ", file); /* not the fixed layout */
    return 0;
}
static int scene_load(unsigned int file) /* the scene's image into the cart: 0 = there, -1 = not on the disc */
{
#if SCD_PLANES
    return scene_load_at(file, 0x080000u, 0, barImgTo, SCD_BOOT_RECORD[5] != 0); /* (an act page not whole: 0) */
#else
    return scene_load_at(file, 0x080000u, 0, 20, true);
#endif
}
#if SCD_PLANES
#include "scd_plasma.h"    /* REV R4: OpenJazz's plasma (GPL-2.0), for the credits */
#include "scd_credits.inc" /* REV R4: the credits screen (plat_credits) */
#else
void plat_credits() {}
#endif
#if SCD_PLANES && SCD_DEVMENU
static char *note_put(char *d, const char *t, char *end)
{
    while (*t && d < end) *d++ = *t++;
    return d;
}
static void menu_note_missing(unsigned int file) /* REV R4: the dev menu's two lines - the file, the stage's name */
{
    char *d = scdMenuNote[0], *end = scdMenuNote[0] + SCD_MENU_NOTE - 1;
    if (activeStageList == 0 && stageListPosition == 0) { /* REV R4B: the title screen - the pause menu's EXIT, a game over:
                                                             the Options Menu is this port's title screen */
        scdMenuNote[0][0] = scdMenuNote[1][0] = 0;
        return;
    }
    char nm[12] = { 'L', (char)('0' + (((file >> 8) - 1) & 7)), 'S', (char)('0' + (file & 0xFF) / 100),
                    (char)('0' + (file & 0xFF) / 10 % 10), (char)('0' + (file & 0xFF) % 10), '.', 'I', 'M', 'G', 0 };
    d  = note_put(d, "NOT ON THE DISC: ", end);
    d  = note_put(d, nm, end);
    *d = 0;
    d  = scdMenuNote[1], end = scdMenuNote[1] + SCD_MENU_NOTE - 1;
    if (activeStageList >= 0 && activeStageList < STAGELIST_MAX && stageListPosition >= 0 && stageListPosition < 0x100)
        d = note_put(d, stageList[activeStageList][stageListPosition].name, end);
    *d = 0;
}
/* REV R4E: CART TEST (the Options Menu) - MicroNut99's 8 / 16 MB cart: two 2 MB windows (window 0 = cart 0x000000-
   0x1FFFFF, the engine's code: never switched; window 1 = 0x200000-0x3FFFFF), each showing one of 8 pages of 2 MB (an
   8 MB cart: pages 4-7 are 0-3 again). The Sub-CPU switches a window with command 46, COMM2 = 0xF800 | window << 8 |
   page (the 68000 writes the page register with RV = 1 and interrupts off); this SH2 purges its cache after each switch.
   Pages 0 and 1 (the 4 MB game's) are only read: page 0 shown in window 1 must be window 0 (the switch works), page 4
   the same as page 0 = an 8 MB cart, a page that shows page 0 is not written (it would be the engine's code). Pages 2-7
   (2-3) each get chunk p of the test file KOBOGFX.BIN (Kobo's file 16: cart 0x200000, window 1) at window 1 + p * 32 KB
   - the Sub-CPU reads each back through the 68000 - then this SH2 reads every page back through its cache and checks
   that no page shows another page's chunk (nor page 1). Window 1 goes back to page 1. The result: the note rows.
   REV R4G: on a 4 MB cart (no mapper) the test tries the Sub-CPU's slot command instead (slot_test). */
#define CART_TEST_FILE 16
#define CART_TEST_CHUNK 0x8000u
static unsigned int cart_pattern(unsigned int c, unsigned int i) /* tools/make_cart_test.py: word i of chunk c */
{
    return (i * 0x9E3779B1u) ^ (0x43540000u | c << 8 | c);
}
static bool cart_chunk_is(unsigned int at, unsigned int c, unsigned int words) /* the test chunk c there (cached reads) */
{
    const unsigned int *p = (const unsigned int *)(0x02000000u + at);
    for (unsigned int i = 0; i < words; ++i)
        if (p[i] != cart_pattern(c, i))
            return false;
    return true;
}
static char *note_pages(char *d, unsigned int mask, char *end) /* " 2 3 5" */
{
    for (int p = 0; p < 8; ++p)
        if (mask >> p & 1) {
            char t[3] = { ' ', (char)('0' + p), 0 };
            d = note_put(d, t, end);
        }
    return d;
}
static char *slot_test(char *d, char *e) /* REV R4G - THE SLOT TEST (a 4 MB cart: no page to switch). The Sub-CPU's
                                            other new case, command 46 COMM2 0xF000 | slot, tried on the cart there is:
                                            the test file (command 38 puts it in slot 16, cart 0x200000) goes to the
                                            first free 128 KB slot after page 0's scene image instead - below the data
                                            pack (0x300000), room the game keeps nothing in - chunk by chunk, each read
                                            back by the Sub-CPU; then this SH2 reads them through its cache, and slot 16
                                            must not show them (the case ignored). Page 0's scene is loaded whole at the
                                            next scene load (were the case ignored, the chunks would be in its image). */
{
    const volatile unsigned int *h = (const volatile unsigned int *)(0x22000000u + 0x080000u); /* the scene's header */
    const unsigned int np          = h[2];
    if (h[0] != 0x53434342u || np == 0 || np >= 64 || h[3 + 5 * np] != SCHK_MAGIC || h[4 + 5 * np] > SCD_CHUNKS_MAX)
        return note_put(d, "SLOT TEST NOT RUN: NO SCENE", e);
    const unsigned int end = 0x080000u + h[4 + 5 * np] * CART_TEST_CHUNK, slot = (end + 0x1FFFFu) >> 17;
    unsigned int n         = slot < 24u ? (24u - slot) * (0x20000u / CART_TEST_CHUNK) : 0u;
    if (n > 8)
        n = 8;
    if (n < 2)
        return note_put(d, "SLOT TEST NOT RUN: NO ROOM", e);
    SC_COMM2 = CART_TEST_FILE;
    if (sd_cmd(38) < 0)
        return note_put(d, "SLOT TEST: NO ANSWER (SUB-CPU)", e);
    if (SC_COMM8_32 < n * CART_TEST_CHUNK)
        return note_put(d, "SLOT TEST NOT RUN: NO KOBOGFX.BIN", e);
    loadLen            = SC_COMM8_32;
    SCD_BOOT_RECORD[5] = 0; /* (page 0's scene: not trusted any more) */
    if (sd_slot(slot) < 0)
        return note_put(d, "SLOT TEST: NO ANSWER (SUB-CPU)", e);
    unsigned int bad68 = 0, badSh2 = 0;
    for (unsigned int c = 0; c < n; ++c)
        if (sd_load_one(c) != 0) /* chunk c at slot * 128 KB + c * 32 KB, read back by the Sub-CPU */
            bad68 |= 1u << c;
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    for (unsigned int c = 0; c < n; ++c)
        if (!cart_chunk_is(slot * 0x20000u + c * CART_TEST_CHUNK, c, CART_TEST_CHUNK / 4))
            badSh2 |= 1u << c;
    const bool ignored = slot != CART_TEST_FILE && cart_chunk_is(CART_TEST_FILE * 0x20000u, 0, 64);
    const char num[3]  = { (char)('0' + slot / 10), (char)('0' + slot % 10), 0 };
    if (ignored)
        return note_put(d, "SLOT CMD IGNORED: DATA IN SLOT 16", e);
    d = note_put(d, "SLOT ", e);
    d = note_put(d, num, e);
    if (!(bad68 | badSh2))
        return note_put(d, " LOAD OK (SUB-CPU AND SH2)", e);
    d = note_put(d, " BAD:", e);
    d = note_pages(d, bad68 | badSh2, e);
    return note_put(d, bad68 ? " (SUB-CPU)" : " (SH2)", e);
}
static int cart_test(); /* REV R4G: plat_cart_test's body */
int plat_cart_test() /* the cart's size if its pages work (16, 8 MB), else 0 - REV R4G: the act pages looked up again */
{
    comm_idle(); /* the pad request (command 48, not waited for) done first: a command written while it runs is lost
                    (sh2emu: the first page switch never reached the Sub-CPU) */
    if (bigN)
        big_show(-1); /* REV R4G: pages 0 and 1 (the menu's text is in the VDP already) */
    if (scene_group(SCD_BOOT_RECORD[5])) { /* REV R4G: page 0 holds an act scene (BIG CART was off, or a title demo
                                              plays): an act page holding the same scene would look like page 0 itself
                                              (the same engine, the same image) - the test from the title, nothing done */
        char *d0 = scdMenuNote[0], *e0 = scdMenuNote[0] + SCD_MENU_NOTE - 1;
        char *d1 = scdMenuNote[1], *e1 = scdMenuNote[1] + SCD_MENU_NOTE - 1;
        d0  = note_put(d0, "CART TEST: AN ACT IS IN PAGE 0", e0);
        d1  = note_put(d1, "RUN IT FROM THE TITLE - NOTHING DONE", e1);
        *d0 = 0, *d1 = 0;
        return 0;
    }
    const int mb = cart_test();
    bigW1        = 1; /* (the test leaves window 1 on page 1) */
    big_forget();     /* it wrote into pages 2-7: what the act pages hold is looked up again */
    return mb;
}
static int cart_test()
{
    char *d0 = scdMenuNote[0], *e0 = scdMenuNote[0] + SCD_MENU_NOTE - 1;
    char *d1 = scdMenuNote[1], *e1 = scdMenuNote[1] + SCD_MENU_NOTE - 1;
    if (sd_page(1, 0) < 0) {
        d0  = note_put(d0, "CART TEST: NO ANSWER (SUB-CPU)", e0);
        *d0 = 0, *d1 = 0;
        return 0;
    }
    if (!cart_same(CART_PROBE, CART_W1 + CART_PROBE, 0x1000)) { /* window 1 did not show page 0 */
        sd_page(1, 1);
        d0  = note_put(d0, "CART TEST: NO PAGE MAPPER (4 MB)", e0);
        d1  = slot_test(d1, e1); /* REV R4G: the slot command instead (R4E/R4F: "NOTHING WRITTEN") */
        *d0 = 0, *d1 = 0;
        return 0;
    }
    sd_page(1, 4);
    const int last = w1_is_page0() ? 3 : 7; /* page 4 = page 0: 8 MB (REV R4G: the engine and the scene image both) */
    unsigned int pages = 0, skip = 0, bad68 = 0, badSh2 = 0, alias = 0;
    for (int p = 2; p <= last; ++p) {
        sd_page(1, p);
        if (w1_is_page0()) /* REV R4G: (an act page holds a copy of the engine: the scene image's header tells them apart) */
            skip |= 1u << p; /* it shows page 0: not written */
        else
            pages |= 1u << p;
    }
    SC_COMM2 = CART_TEST_FILE;
    if (sd_cmd(38) < 0 || SC_COMM8_32 < (unsigned int)(last + 1) * CART_TEST_CHUNK) {
        sd_page(1, 1);
        d0  = note_put(d0, "CART TEST: KOBOGFX.BIN NOT ON THE DISC", e0);
        *d0 = 0, *d1 = 0;
        return 0;
    }
    loadLen = SC_COMM8_32;
    for (int p = 2; p <= last; ++p)
        if (pages >> p & 1) {
            sd_page(1, p);
            if (sd_load_one((unsigned int)p) != 0) /* chunk p at window 1 + p * 32 KB, read back by the Sub-CPU */
                bad68 |= 1u << p;
        }
    for (int p = 2; p <= last; ++p)
        if (pages >> p & 1) {
            sd_page(1, p);
            if (!cart_chunk_is(CART_W1 + p * CART_TEST_CHUNK, (unsigned int)p, CART_TEST_CHUNK / 4))
                badSh2 |= 1u << p;
            for (int q = 2; q <= last; ++q)
                if (q != p && (pages >> q & 1) && cart_chunk_is(CART_W1 + q * CART_TEST_CHUNK, (unsigned int)q, 64))
                    alias |= 1u << p; /* page p shows page q's chunk: the same RAM */
        }
    sd_page(1, 1);
    for (int q = 2; q <= last; ++q)
        if ((pages >> q & 1) && cart_chunk_is(CART_W1 + q * CART_TEST_CHUNK, (unsigned int)q, 64))
            alias |= 1u << 1; /* page 1 shows a test chunk: written through another page's number */
    const unsigned int ok = pages & ~(bad68 | badSh2 | alias);
    d0 = note_put(d0, last == 7 ? "CART 16 MB: " : "CART 8 MB: ", e0);
    const bool allOk = ok == pages && !skip && !(alias & 2);
    if (allOk) {
        d0 = note_put(d0, last == 7 ? "PAGES 2-7 OK" : "PAGES 2-3 OK", e0);
        d1 = note_put(d1, "PAGE SWITCH OK, GAME PAGES UNTOUCHED", e1);
    }
    else {
        d0 = note_put(d0, "OK", e0);
        d0 = ok ? note_pages(d0, ok, e0) : note_put(d0, " NONE", e0);
        if (bad68 | badSh2) {
            d1 = note_put(d1, "BAD", e1);
            d1 = note_pages(d1, bad68 | badSh2, e1);
            d1 = note_put(d1, " ", e1);
        }
        if (alias) {
            d1 = note_put(d1, "SAME RAM", e1);
            d1 = note_pages(d1, alias, e1);
            d1 = note_put(d1, " ", e1);
        }
        if (skip) {
            d1 = note_put(d1, "AS PAGE 0:", e1);
            d1 = note_pages(d1, skip, e1);
        }
    }
    *d0 = 0, *d1 = 0;
    return allOk ? (last == 7 ? 16 : 8) : 0;
}
#else
int plat_cart_test() { return 0; }
#endif
bool plat_stage_loading()
{
#if SCD_PLANES
    pm_end(true); /* REV R4C: a scene in the 32X picture mode ends: the loading picture */
    pmScene = false;
    if (activeStageList == STAGELIST_PRESENTATION && stageListPosition >= 0 && stageListPosition < stageListCount[STAGELIST_PRESENTATION] &&
        name_is(stageList[STAGELIST_PRESENTATION][stageListPosition].folder, "Credits")) {
        /* REV R4C: the game's own credits (after the ending, or from its menu): this port's credits screen, then the
           title screen */
        plat_credits();
        activeStageList   = STAGELIST_PRESENTATION;
        stageListPosition = 0;
        ResetCurrentStageFolder();
    }
    /* REV R1: the planes off (the Sub-CPU's loading lines) - REV R4B: the loading picture in front, the Genesis display
       off; the scene and its act file in the cart already (a restart): black, nothing to wait for */
    planes_off(scene_resident(scene_file(), mdg_file()) ? PLANES_OFF_BLACK : PLANES_OFF_PICTURE); /* REV R4G: or in an act
                                                                                                     page */
    menuDrawn = false;
#endif
    audio_before_load(); /* REV R4 */
    const unsigned int *h = (const unsigned int *)CART_STAGE;
    if (!haveOffsets) { /* the layout the engine bound at start-up (the first scene, loaded by the loader) */
        for (int k = 0; k < (int)h[2] && k < 32; ++k) firstOffsets[k] = h[3 + 5 * k];
        haveOffsets = true;
        big_detect(); /* REV R4G: the 16 MB cart? (after power-on: page 0 holds the boot loader's title) */
        big_boot_preload(); /* REV R4L: the menu and a new game's first act set, before the title starts */
    }
    STATE(0x5C13);
    unsigned int file = scene_file();
#if SCD_PLANES && SCD_DEVMENU
    /* REV R4H: the pause menu's EXIT (and a game over) go to the title: there the Options Menu instead - the title is
       loaded into page 0 first (so the cart test finds the title there, not an act), then the menu */
    static bool fromAct  = false;
    const bool toOptions = fromAct && activeStageList == 0 && stageListPosition == 0;
    fromAct              = (scene_group(file) != 0 || (file & 0xF00u) == 0x400u) && /* REV R5C: a special stage's EXIT too */
              GetGlobalVariableByName("Options.AttractMode") == 0;
#endif
    big_prepare(file); /* REV R4G: the act page the scene is in or goes to; an act's start: its preload planned */
    int bad = scene_load(file);
#if SCD_PLANES
    if (bad == 0) { /* REV R4B: the scene is in the cart (loaded now, or there already): the bar's first part full */
        splash_part(0, barImgTo);
        splash_chunk(1, 1);
        big_preload(); /* REV R4G: the act's other periods and the next act's start, into the other act pages */
    }
#endif
#if SCD_PLANES && SCD_DEVMENU
    if (bad < 0) {
        /* REV R4: the scene is not on the disc (the title screen, a special stage, a scene not built): the dev menu, with
           the file and the stage's name - the engine loads nothing (Scene.cpp LoadStageFiles) */
        menu_note_missing(file);
        STATE(0x5C17);
        Engine.gameMode = ENGINE_INITDEVMENU;
        return false;
    }
    scdMenuNote[0][0] = scdMenuNote[1][0] = 0;
#elif defined(SCD32X_START_LIST)
    if (bad < 0) {
        /* REV R3: the scene is not on the disc - the act's end (R1X stopped here: NO SCENE 204), a time warp or a
           special stage. The start scene again, with the other character: Sonic, then Tails, then Sonic... The
           whole stage loads again (its folder is the one already loaded, so the engine would only reload the
           act): the other character's scripts load the other animation files, from the other image. */
        activeStageList   = SCD32X_START_LIST;
        stageListPosition = SCD32X_START_STAGE;
        playerListPos     = playerListPos == 1 ? 0 : 1;
        ResetCurrentStageFolder();
        file = scene_file();
        bad  = scene_load(file);
        if (bad < 0 && playerListPos == 1) { /* no Tails files on this disc: Sonic again */
            playerListPos = 0;
            file          = scene_file();
            bad           = scene_load(file);
        }
    }
#endif
    if (bad < 0)
        scene_stop(0x5CE8, "NO SCENE ", file); /* the scene's .IMG is not on the disc */
#if SCD_PLANES && SCD_DEVMENU
    if (toOptions) { /* REV R4H: (above) the title in the cart, the engine loads nothing: the Options Menu - REV R4M: at
                        STAGE SELECT (the owner: Pause > EXIT is the level select; B there: the Options Menu) */
        STATE(0x5C19);
        scdMenuStages = 1;
        scdMenuNote[0][0] = scdMenuNote[1][0] = 0;
        Engine.gameMode   = ENGINE_INITDEVMENU;
        return false;
    }
#endif
#if SCD_PLANES
    { /* REV R4C: an image with the sheets' and the tiles' pixels is a scene of the 32X picture mode - from its load on
         (the layout pool, the tiles its scripts copy) */
        const unsigned int *ih = (const unsigned int *)CART_STAGE;
        pmScene = ih[2] > SCD_CART_TILEGFX && ih[3 + 5 * SCD_CART_GFXDATA + 1] > 0 && ih[3 + 5 * SCD_CART_TILEGFX + 1] > 0;
        if (pmScene) {
            pmGfx   = (const unsigned char *)plat_cart_data(SCD_CART_GFXDATA);
            pmTiles = (const unsigned char *)plat_cart_data(SCD_CART_TILEGFX);
        }
        scdPicture = pmScene;
    }
#endif
    STATE(0x5C16); /* R1C: the engine's own stage load runs from here (until 5Lnn) */
    comm_idle();   /* REV R4: the pad answer taken now - the sampler writes COMM8 (CKPT) from here on */
    scd_sampling(1);
    return true;
}
void plat_stage_loaded() /* line 10: STATE:5Lnn = list L, scene nn (5000 = the title) */
{
    scd_sampling(0); /* R1C: COMM12 is a reply register again (MDG load, command 68) */
    for (int k = 0; k < 4; ++k) {
        unsigned int f = VBLANK_COUNT;
        while (VBLANK_COUNT == f) {
        }
    } /* a sample posted just now must not overwrite the code below */
    STATE(0x5000 | ((unsigned int)activeStageList << 8) | ((unsigned int)stageListPosition & 0xFF));
    scdDemoFrom = presented; /* REV R4: the scripted pad from the stage's start */
#if SCD_PLANES
    if (pmScene)
        pm_begin(); /* REV R4C: the 32X picture mode (no act file) */
    else
        planes_load(); /* REV R1: the scene's Genesis planes (a failure leaves its 5C6x code on line 10) */
#endif
}
void plat_file_read(const char *, int) {} // PC builds only
void plat_draw_log(const char *, int, int) {} // S7e: PC builds only
void plat_chunk_arrays(const unsigned short **tileIndex, const unsigned char **direction, const unsigned char **visualPlane,
                       const unsigned char **solidity, int *n)
{
    const unsigned char *p = (const unsigned char *)plat_cart_data(SCD_CART_CHUNKS);
    int c                  = (int)*(const unsigned int *)p;
    *tileIndex             = (const unsigned short *)(p + 4);
    *direction             = p + 4 + 2 * c;
    *visualPlane           = p + 4 + 3 * c;
    solidity[0]            = p + 4 + 4 * c;
    solidity[1]            = p + 4 + 5 * c;
    *n                     = c;
}

// the frame buffer: record 12 KB | menus 34 KB | layout pool 82 KB (cleared once: it is not zeroed
// at power-on)
#define SCD_FB_RECORD_BYTES (12 * 1024)
#define SCD_FB_MENUS_AT 0x24003000
#define SCD_FB_MENUS_BYTES (34 * 1024)
#define SCD_FB_POOL_AT (SCD_FB_MENUS_AT + SCD_FB_MENUS_BYTES)
#define SCD_FB_POOL_BYTES (0x20000 - SCD_FB_RECORD_BYTES - SCD_FB_MENUS_BYTES)
static void fb_clear(unsigned int at, int bytes)
{
    volatile unsigned short *p = (volatile unsigned short *)at;
    for (int i = 0; i < bytes / 2; ++i) p[i] = 0;
}
#if SCD32X_DRAWCART
// S7e: the drawing build - the frame buffer holds the game's picture; menus and the layout pool in SDRAM
static unsigned char sdramMenus[2 * sizeof(TextMenu)] __attribute__((aligned(4)));
static unsigned char sdramPool[6 * 1024] __attribute__((aligned(4))); /* the title needs 5 KB */
unsigned char *plat_fb_data(int kind, int size) { return (kind == SCD_FB_MENUS && size <= (int)sizeof(sdramMenus)) ? sdramMenus : (unsigned char *)0; }
unsigned char *plat_layout_pool(int *size)
{
    *size = (int)sizeof(sdramPool);
    return sdramPool;
}
#define SCD_FB_ELSEWHERE 1
#endif
#ifndef SCD_FB_ELSEWHERE
#define SCD_FB_LINE_BYTES 1024 /* REV R4: the no-draw build's one-line buffer at the pool's end (the pool: 81 KB) */
unsigned char *plat_fb_data(int kind, int size)
{
    static bool cleared = false;
    if (kind == SCD_FB_LINE)
        return size <= SCD_FB_LINE_BYTES ? (unsigned char *)(SCD_FB_POOL_AT + SCD_FB_POOL_BYTES - SCD_FB_LINE_BYTES) : (unsigned char *)0;
    if (kind != SCD_FB_MENUS || size > SCD_FB_MENUS_BYTES)
        return (unsigned char *)0;
    if (!cleared) {
        cleared = true;
        fb_clear(SCD_FB_MENUS_AT, SCD_FB_MENUS_BYTES);
    }
    return (unsigned char *)SCD_FB_MENUS_AT;
}
unsigned char *plat_layout_pool(int *size)
{
#if SCD_PLANES
    if (pmScene) { /* REV R4C: the 32X picture mode's pool, after its picture (cd32x/engine/scd_pm.h) */
        *size = PM_POOL_BYTES;
        return (unsigned char *)(0x24000000u + PM_FB_POOL);
    }
#endif
    *size = SCD_FB_POOL_BYTES - SCD_FB_LINE_BYTES; /* stages_report.md: regular and special stages use 24 KB at most */
    return (unsigned char *)SCD_FB_POOL_AT;
}
#endif

// ---------------------------------------------------------------- the data pack: the stage's mini-pack
static ScdMemFile memFiles[4];
static bool memUsed[4];
ScdMemFile *scd_fopen(const char *path, const char *mode)
{
    int n = (int)strlen(path);
    if (mode[0] != 'r' || n < 5)
        return NULL;
    for (int i = 0; i < 5; ++i) { /* ends in ".rsdk", any case (S7b3: no strcasecmp - <strings.h> on the SH2) */
        char c = path[n - 5 + i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != ".rsdk"[i])
            return NULL; /* only the data pack exists; settings / saves fall back to defaults */
    }
    /* REV R4: the scene image's own pack (part 17): every scene carries the files the SH2 reads in it - one shared pack
       (SCDDATA.PAK, 512 KB at cart 0x300000) cannot hold every stage's. The engine opens the pack for every file it
       loads, so after a scene change it reads the new scene's. */
    const unsigned int *h = (const unsigned int *)CART_STAGE;
    const unsigned char *pack = CART_PACK;
    long packSize             = (long)SCD_BOOT_RECORD[2]; /* S7d: SCDDATA.PAK's length (an image without part 17) */
    if (h[2] > SCD_CART_DATAPACK && h[3 + 5 * SCD_CART_DATAPACK + 1] > 0) {
        pack     = CART_STAGE + h[3 + 5 * SCD_CART_DATAPACK];
        packSize = (long)h[3 + 5 * SCD_CART_DATAPACK + 1];
    }
    for (int i = 0; i < 4; ++i)
        if (!memUsed[i]) {
            memUsed[i]       = true;
            memFiles[i].data = pack;
            memFiles[i].size = packSize;
            memFiles[i].pos  = 0;
            return &memFiles[i];
        }
    return NULL;
}
size_t scd_fread(void *buffer, size_t elementSize, size_t elementCount, ScdMemFile *f)
{
    if (!f || !elementSize)
        return 0;
    long want = (long)(elementSize * elementCount), left = f->size - f->pos;
    if (want > left)
        want = left < 0 ? 0 : left;
    want -= want % (long)elementSize;
    memcpy(buffer, f->data + f->pos, (size_t)want);
    f->pos += want;
    return (size_t)want / elementSize;
}
int scd_fseek(ScdMemFile *f, long offset, int whence)
{
    long p = whence == SEEK_SET ? offset : whence == SEEK_CUR ? f->pos + offset : f->size + offset;
    if (p < 0 || p > f->size)
        return -1;
    f->pos = p;
    return 0;
}
long scd_ftell(ScdMemFile *f) { return f ? f->pos : -1; }
int scd_fclose(ScdMemFile *f)
{
    for (int i = 0; i < 4; ++i)
        if (f == &memFiles[i])
            memUsed[i] = false;
    return 0;
}

// C++ runtime pieces the engine needs, without exceptions (S5b): libstdc++'s operator new can throw
// std::bad_alloc, which pulls in the exception unwinder (unwind-dw2, ~15 KB), and its default
// terminate handler demangles names to print them (cp-demangle, ~29 KB). Plain versions here.
#include <new>
#include <stdlib.h>
// operator new / delete: at the end of this file (S7b7 - they stop with their caller)
extern "C" void __cxa_pure_virtual()
{
    for (;;) {
    }
}
namespace __gnu_cxx {
void __verbose_terminate_handler()
{
    for (;;) {
    }
}
} // namespace __gnu_cxx

// C++ start-up symbols without a hosted runtime (the program will be started by Kobo's crt0.s)
extern "C" {
void *__dso_handle = 0;
void _init(void) {}
void _fini(void) {}

// System calls. The SH C library brings its own (lib_a-syscalls.o: simulator traps), which also
// defines errno a second time - the S5a link failed on that. Defining them here keeps that
// object out; on the console there is nothing to trap into anyway. (S5a fix)
#include <sys/stat.h>
#include <sys/types.h>
extern char end[];                              /* end of the program's data (linker) */
/* S7b7: NO heap on the SH2 (Kobo rule 8). Our own allocation entry points replace the library's
   (this object is linked first; --allow-multiple-definition). Each one stops with the size asked and
   its EXACT caller - __builtin_return_address(0), the SH2's PR at entry: for operator new that is
   the engine function, for malloc / _malloc_r it may be a library function (then that one's name
   tells what it was doing). S7b6's stack search found a stale address - replaced. */
static void no_heap(unsigned int size, void *caller)
{
    char t[16], *p = t;
    scd_text_on(); /* REV R4B: the Genesis text on (line 10) */
    STATE(0x5CE4);
    for (const char *a = "NO HEAP A"; *a;) *p++ = *a++;
    hex(p, size, 4);
    scd_status(0, t, PX_RED);
    p = t;
    for (const char *a = "AT "; *a;) *p++ = *a++;
    hex(p, (unsigned int)(unsigned long)caller, 8);
    scd_status(1, t, PX_RED);
    for (;;) {
    }
}
void *malloc(size_t n) { no_heap((unsigned int)n, __builtin_return_address(0)); return 0; }
void *calloc(size_t a, size_t b) { no_heap((unsigned int)(a * b), __builtin_return_address(0)); return 0; }
void *realloc(void *, size_t n) { no_heap((unsigned int)n, __builtin_return_address(0)); return 0; }
void free(void *) {}
void *_malloc_r(struct _reent *, size_t n) { no_heap((unsigned int)n, __builtin_return_address(0)); return 0; }
void *_calloc_r(struct _reent *, size_t a, size_t b) { no_heap((unsigned int)(a * b), __builtin_return_address(0)); return 0; }
void *_realloc_r(struct _reent *, void *, size_t n) { no_heap((unsigned int)n, __builtin_return_address(0)); return 0; }
void _free_r(struct _reent *, void *) {}
void *_sbrk(ptrdiff_t n) /* only if something reached the library's allocator anyway */
{
    no_heap((unsigned int)n, __builtin_return_address(0));
    return (void *)-1;
}
int _write(int, const void *, size_t n) { return (int)n; }
int _read(int, void *, size_t) { return 0; }
int _open(const char *, int, ...) { return -1; }  /* no file system: the data comes from the cart (S5c) */
int _close(int) { return -1; }
off_t _lseek(int, off_t, int) { return 0; }
int _fstat(int, struct stat *st)
{
    st->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int) { return 1; }
int _kill(int, int) { return -1; }
int _getpid(void) { return 1; }
/* the rest of newlib's SH syscalls.c, so that file has nothing left to offer and stays out */
#include <sys/time.h>
#include <sys/times.h>
int _creat(const char *, int) { return -1; }
int _stat(const char *, struct stat *st)
{
    st->st_mode = S_IFCHR;
    return -1;
}
clock_t _times(struct tms *) { return (clock_t)-1; }
int _gettimeofday(struct timeval *tv, void *)
{
    tv->tv_sec  = 0;
    tv->tv_usec = 0;
    return 0;
}
int _unlink(const char *) { return -1; }
int _link(const char *, const char *) { return -1; }
int _chmod(const char *, short) { return -1; }
int _chown(const char *, short, short) { return -1; }
int _utime(const char *, void *) { return -1; }
int _fork(void) { return -1; }
int _wait(int *) { return -1; }
int _execve(const char *, char *const *, char *const *) { return -1; }
int _pipe(int *) { return -1; }
int _raise(int) { return -1; }
void _exit(int)
{
    for (;;) {
    }
}
}

// S7b7: C++ allocation - the caller is the engine function itself
void *operator new(size_t n) { no_heap((unsigned int)n, __builtin_return_address(0)); return 0; }
void *operator new[](size_t n) { no_heap((unsigned int)n, __builtin_return_address(0)); return 0; }
void operator delete(void *) noexcept {}
void operator delete[](void *) noexcept {}
void operator delete(void *, size_t) noexcept {}
void operator delete[](void *, size_t) noexcept {}
