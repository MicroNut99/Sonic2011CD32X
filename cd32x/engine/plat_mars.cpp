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
/* R1C: the loader's PC sampler (sh2_engboot.c, boot record [7] = &scdSampling). On, every 30 vertical
   blanks line 10 shows where the SH2 is: 8xxx = cart code 0x02000000 + xxx * 16, 7xxx = SDRAM. Only on
   while nothing waits for a COMM12 reply (scene loads and SH2 command 68 use COMM12). */
static void scd_sampling(unsigned int on)
{
    unsigned int a = SCD_BOOT_RECORD[7];
    if (a >= 0x06000000u && a < 0x06002000u)
        *(volatile unsigned int *)a = on;
}
#define STATE(code) (*(volatile unsigned short *)0x2000402C = (unsigned short)(code)) /* Kobo's STATE (COMM12) */

void scd_status(int row, const char *t, int color);
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
#define SGB_CART_OFFSET 0x380000u
#define SGB_FILE ((const unsigned char *)0x02380000) /* cached: purged after every load */
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
static MdPlane sgbA, sgbB;
static int sgbBLayer = -1;
static bool slotUsed[4];
static unsigned char slotLayer[4];
static int tileYA, tileYB, frameBg;
static int xA[SCREEN_YSIZE], xB[SCREEN_YSIZE];
static int16_t hsA[SCREEN_YSIZE], hsB[SCREEN_YSIZE];
static unsigned short cramSent[64];
static unsigned short recId = 0;
static MdSprites sprites; /* REV R3: the act's sprites as Genesis hardware sprites (md/md_sprites.h) */
static void sgb_pause()
{
    for (volatile int k = 0; k < 64; ++k) {
    } /* Kobo HW6: pause between COMM reads */
}
static int sgb_cmd(unsigned short op, unsigned int arg) /* SH2 command 68 -> reply (COMM12) */
{
    SC_COMM2    = op;
    SC_COMM8_32 = arg;
    SC_COMM12   = 0xFFFF;
    SC_COMM0    = 68;
    for (unsigned int n = 0; n < 2000000u && SC_COMM0 != 0; ++n) sgb_pause();
    return SC_COMM12;
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
static void planes_off()
{
    if (!planesOn)
        return;
    planesOn = false;
    sgb_cmd(2, 0); /* the listener's own picture back (text, for the loading lines) */
}
/* REV R3: Tails. A scene played as Tails (the game's character choice: playerListPos 1) has files of its own,
   numbered 128 on: its scene image L<list>S<pos + 128>.IMG (the cart image holds the scene's animation files and
   sprite sheets in the order the scripts load them - Tails's) and its act file G<list>S<pos + 128>.MDG (its sprite
   pack has Tails's frames). tools/build_game_data.py and tools/build_mdg.sh make them ("1:0@1"). */
static unsigned int player_files()
{
    return (playerListPos == 1 && stageListPosition >= 0 && stageListPosition < 0x80) ? 0x80u : 0u;
}
static void planes_load() /* the scene's MDG: on the disc? then into the cart and the VDP */
{
    unsigned int file  = 0x1000 + (((unsigned int)activeStageList + 1) << 8) + (unsigned int)stageListPosition + player_files();
    unsigned int bytes = 0;
    STATE(0x5C60); /* R1C: the MDG load starts */
    int bad            = ((ScdLoadFile)SCD_BOOT_RECORD[6])((unsigned short)file, &bytes);
    if (bad < 0) {
        STATE(0x5C61);
        return;
    }
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    const unsigned char *f = SGB_FILE;
    if (bad > 0 || *(const unsigned int *)f != SGB_MAGIC || *(const unsigned short *)(f + 4) != SGB_VERSION ||
        *(const unsigned int *)(f + 8) != bytes) {
        STATE(0x5C62);
        return;
    }
    STATE(0x5C65); /* R1C: the MDG is in the cart; command 68 next */
    SC_COMM4 = 0; /* R1S (R1P's line): COMM4 still held the Sub-CPU's start-up check 0x1234 - the 68000 took it as a
                     record id the moment command 68 switched the records on and copied junk from the frame buffer
                     (R1H: ID 1234, REC 0004 0004 0006 ...): R1G's and R1R's dot in every blank cell. Cleared here by
                     the SH2, before command 68. */
    if (sgb_cmd(1, SGB_CART_OFFSET) != 1) {
        STATE(0x5C63);
        return;
    }
    const unsigned short *cram0 = (const unsigned short *)(f + *(const unsigned int *)(f + 32));
    for (int i = 0; i < 64; ++i) cramSent[i] = cram0[i];
    /* REV R3: the sprite pack after the plane data ('SPRP' + offset in the header's last 8 bytes); its cells go
       after the act's plane cells, up to plane A's name table (0xC000) */
    mds_init(&sprites, *(const unsigned int *)(f + 56) == MDS_TAG ? f + *(const unsigned int *)(f + 60) : 0,
             *(const unsigned short *)(f + 6), 0xC000 / 32);
    md_plane_reset(&sgbA);
    md_plane_reset(&sgbB);
    sgbBLayer = -1;
    for (int s = 0; s < 4; ++s) slotUsed[s] = false;
    frameBg  = -1;
    planesOn = true;
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* 32X layer blank: the Genesis shows */
}
static bool sgb_shows(int slots, bool front)
{
    for (int s = 0; s < 4; ++s)
        if ((slots >> s & 1) && (s >= tLayerMidPoint) == front)
            return true;
    return false;
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
        sprites.ndraw = 0; /* REV R3: the sprites keep the last picture too */
        return;            /* the planes keep the last picture */
    }
    const unsigned char *f = SGB_FILE;
    MdLayer LA, LB;
    LA.chunks       = (const uint16_t *)(f + *(const unsigned int *)(f + 24));
    LA.quads        = (const uint16_t *)(f + *(const unsigned int *)(f + 20));
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
    md_plane_update(&sgbA, &LA, SGB_NAME_A, tileYA, xA, SCREEN_YSIZE, SCREEN_XSIZE, hsA, &vA, sgb_write, &o);
    if (bgLayer >= 0) {
        if (bgLayer != sgbBLayer) {
            md_plane_reset(&sgbB);
            sgbBLayer = bgLayer;
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
    const unsigned short *slots = (const unsigned short *)(f + *(const unsigned int *)(f + 16));
    uint16_t cIdx[64], cCol[64];
    int nc = 0;
    for (int i = 0; i < 64; ++i) {
        if (slots[i] == 0xFFFF)
            continue;
        uint16_t c565 = fullPalette[0][slots[i]];
        if (fadeMode > 0) /* REV R3: the engine's fade rectangle, on the palette - planes and sprites fade with it */
            c565 = sgb_fade565(c565, fadeR, fadeG, fadeB, fadeA);
        uint16_t c = sgb_md_color(c565);
        if (c != cramSent[i]) {
            cIdx[nc]    = (uint16_t)i;
            cCol[nc++]  = c;
            cramSent[i] = c;
        }
    }
    if (sgb_out_end(&o, vA, vB, hsA, hsB, SCREEN_YSIZE, cIdx, cCol, nc) < 0) {
        md_plane_reset(&sgbA); /* too big: both planes from scratch next frame */
        md_plane_reset(&sgbB);
        mds_forget(&sprites); /* REV R3: its pattern uploads never reached the VRAM */
        return;
    }
    /* hand it over: give_fb, COMM4 = id; the 68000 copies it and answers COMM6 = id; take_fb */
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
        planes_off();
        STATE(0x5C64);
    }
}
#endif
#if SCD32X_DRAWCART
static void draw_show(); // S7e (below)
#endif
void plat_present(const unsigned short *, int, int, int) SCD_HOT; /* R1V: SDRAM */
void plat_present(const unsigned short *, int, int, int)
{
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
    if (planesOn)
        planes_frame(); /* REV R1 */
#endif
    unsigned int f = VBLANK_COUNT;
    while (VBLANK_COUNT == f) {
    } /* pace: at most one engine frame per vertical blank */
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
#if SCD_DEMO_PAD
// REV R1 test disc: the PC check's pad (tools/run_md_check.sh) - after 4 s, every 4 s: spin dash
// (hold down, press C, release), run right 3 s with one jump - so Sonic moves and the camera scrolls
unsigned int plat_pad() SCD_HOT; /* R1W: SDRAM */
unsigned int plat_pad()
{
    unsigned int ms = presented * 1000 / 60;
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
#else
unsigned int plat_pad() { return 0; }
#endif
void plat_music_play(const char *, int, unsigned int) {}
void plat_music_stop() {}
void plat_sfx_play(int, int) {}
void plat_sfx_loaded(int, const char *, int, int, int, int) {}
#if SCD_PLANES
// REV R1: each drawn layer slot's scroll (DrawHLineScrollLayer) - layer 0 (the foreground) for
// plane A, the first other layer for plane B (two background layers at once: no record)
unsigned short *plat_layer_begin(int slot, int layer, int tileYPos, unsigned short *frame) SCD_HOT; /* R1W: SDRAM */
unsigned short *plat_layer_begin(int slot, int layer, int tileYPos, unsigned short *frame)
{
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
void plat_layer_end(int) {}
int *plat_layer_lines(int slot) SCD_HOT; /* R1W: SDRAM */
int *plat_layer_lines(int slot) /* R1T: as plat_layer_line - layer 0 into xA, the frame's background layer into xB */
{
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
static int scene_load(unsigned int file) /* the scene's image into the cart: 0 = there, -1 = not on the disc */
{
    if (file == SCD_BOOT_RECORD[5])
        return 0; /* in the cart already */
    unsigned int bytes = 0;
    int bad = ((ScdLoadFile)SCD_BOOT_RECORD[6])((unsigned short)file, &bytes);
    if (bad < 0)
        return -1; /* the Sub-CPU found no such file: nothing was copied, the cart holds the last image */
    if (bad > 0)
        scene_stop(0x5CE2, "BAD LOAD ", file);
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    SCD_BOOT_RECORD[5] = file;
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
    const unsigned int *h = (const unsigned int *)CART_STAGE;
    for (int k = 0; k < (int)h[2] && k < 32; ++k)
        if (h[3 + 5 * k] != firstOffsets[k])
            scene_stop(0x5CE9, "LAYOUT ", file); /* not the fixed layout */
    return 0;
}
void plat_stage_loading()
{
#if SCD_PLANES
    planes_off(); /* REV R1: the loading lines need the Genesis text again */
#endif
    const unsigned int *h = (const unsigned int *)CART_STAGE;
    if (!haveOffsets) { /* the layout the engine bound at start-up (the first scene, loaded by the loader) */
        for (int k = 0; k < (int)h[2] && k < 32; ++k) firstOffsets[k] = h[3 + 5 * k];
        haveOffsets = true;
    }
    STATE(0x5C13);
    unsigned int file = scene_file();
    int bad           = scene_load(file);
#if defined(SCD32X_START_LIST)
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
    STATE(0x5C16); /* R1C: the engine's own stage load runs from here (until 5Lnn) */
    scd_sampling(1);
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
#if SCD_PLANES
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
unsigned char *plat_fb_data(int kind, int size)
{
    static bool cleared = false;
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
    *size = SCD_FB_POOL_BYTES;
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
    for (int i = 0; i < 4; ++i)
        if (!memUsed[i]) {
            memUsed[i]       = true;
            memFiles[i].data = CART_PACK;
            memFiles[i].size = (long)SCD_BOOT_RECORD[2]; /* S7d: SCDDATA.PAK's length */
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
