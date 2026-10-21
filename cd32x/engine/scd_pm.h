// REV R4C: the 32X PICTURE MODE - the engine's own pictures, drawn by the SH2 into the 32X frame buffer, for the scenes
// the Genesis planes cannot show: the title, the menus, time attack, the stage select, sound test, D.A. Garden, help,
// Tails's unlock screen and the special stages (their 3D, their scaled and rotated sprites, more than 80 sprites).
//
// The picture is the 32X's 256-colour mode: one byte a pixel, the engine's own palette index - the colours are the
// engine's palette (activePalette, the scene's bank) written into the 32X's colour RAM each vertical blank, with the
// engine's fade. Lines are GFX_LINESIZE (328) bytes apart, as the engine's own frame buffer, so every draw call clips
// and steps exactly as the engine's software renderer does; the 32X shows the first 320 pixels of each.
//
// Each function here is the engine's RENDER_SW drawing (engine/Drawing.cpp, engine/Scene3D.cpp) with its pixel writes
// replaced. The frame buffer takes 16-bit writes only (byte writes are unreliable on the console - the OpenJazz
// addendum, rule 3): two pixels go out as one word into the frame buffer's OVERWRITE IMAGE, which leaves a 0 byte's pixel
// as it was - a sprite's see-through pixels cost nothing, and a single pixel is a word with a 0 beside it. Colours a
// draw call computes (rectangles, faces) are never palette index 0 for that reason (pm_nearest). What 256 colours cannot
// do the 16-bit renderer does - mixing two colours - is an ordered dither (a 4 x 4 Bayer pattern): the blended (half
// see-through) sprites, the alpha-blended and additive ones, faces and rectangles with alpha. A rectangle over the whole
// screen with alpha is the engine's fade (DrawStageGFX's last rectangle, the scripts' flashes): it goes into the colours
// instead when it is the frame's last draw. The special stages' floor (Draw3DSkyLayer) samples once per pixel what the
// engine samples four times (the last of them that is not see-through, as the engine's last write wins). Tiles a script
// copies (Copy16x16Tile: the animated floor) are a table of which tile's pixels each tile shows (the tile pixels are in
// the cart, which the SH2 cannot write): the same picture as copying the pixels.
//
// Plain C++ over the engine's own data; the includer provides the memory (before including):
//   PM_W16(off, w) PM_O16(off, w)  a word of the picture / of its overwrite image at byte offset off (even; line y at
//                                  y * 328)
//   PM_O8(off, v)                  one pixel v (never 0) at off (a word into the overwrite image, 0 beside it)
//   PM_GFX                         const byte *: the sprite sheets' pixels (graphicData)
//   PM_TILES                       const byte *: the tile pixels as the scene loaded them (tilesetGFXData)
//   PM_CHUNK_TILE(e), PM_CHUNK_DIR(e), PM_CHUNK_PLANE(e)   chunk entry e: its tile, flip, plane
//   PM_DIV(a, b)                   (optional) a / b
//   PM_VERTS                       Vertex *: 0x280 transformed vertices a frame may use (the 3D scene)
//   PM_SCAN                        int *: 6 x 224 - a face's scan lines (the engine's faceLineStart ... faceLineEndV)
//   PM_LIST                        DrawListEntry3D *: 0xA0 entries (the 3D scene's sorted faces)
// The SH2 engine (cd32x/engine/plat_mars.cpp) points them at the frame buffer and the cart image; the PC check
// (host/plat_host.cpp, SCD32X_PMCHECK) at its own arrays, and compares the result with the engine's 16-bit picture.
#ifndef SCD_PM_H
#define SCD_PM_H

#define PM_STRIDE 328 /* GFX_LINESIZE (SetScreenSize: (320 + 9) & -8) */
#define PM_LINES 224  /* SCREEN_YSIZE */
#define PM_VERTS_MAX 0x280
#define PM_FACES_MAX 0xA0
/* the SH2's frame buffer in the picture mode (each of the two, 128 KB): the line table, the picture, the 3D scene's
   vertices (written and read in one frame: each buffer its own), the layouts (the same in both: copied when the scene
   starts, a script's change written into both - cd32x/engine/plat_mars.cpp), the engine's one-line buffer (SCD_FB_LINE) */
#define PM_FB_PIXELS 0x00200
#define PM_FB_VERTS 0x12100
#define PM_FB_SCAN (PM_FB_VERTS + PM_VERTS_MAX * 20)
#define PM_FB_POOL (PM_FB_SCAN + 6 * PM_LINES * 4)
#define PM_FB_POOL_END 0x1FC00
#define PM_POOL_BYTES (PM_FB_POOL_END - PM_FB_POOL)
#ifndef PM_APPROX
#define PM_APPROX(off, n) ((void)0) /* the PC check: n pixels from off are an approximation (dither, nearest colour) */
#endif
#ifndef PM_BE16 /* a big-endian 16-bit word at an even address (the SH2: one read; the PC check: two) */
#define PM_BE16(p) ((unsigned short)((p)[0] << 8 | (p)[1]))
#endif
/* REV R4D: a draw call's rows shared with the 32X's second SH2 - the includer's PM_SPLIT(fn, job) runs fn(job, 1, 2)
   there and fn(job, 0, 2) here, both done before it returns (the rows are disjoint: the same picture); PM_SPLIT_FN(fn, J)
   after each such fn (what the other SH2 calls). Without them: fn(job, 0, 1), every row here (the PC check) */
#ifndef PM_SPLIT
#define PM_SPLIT(fn, job) fn(job, 0, 1)
#endif
#ifndef PM_SPLIT_FN
#define PM_SPLIT_FN(fn, J)
#endif
#define PM_SPLIT_MIN 24       /* a face or sprite shorter than this: all of it here (the hand-over costs more than it saves) */
#define PM_SPLIT_PIXELS 4096  /* a sprite smaller than this: all of it here */
/* a value the compiler must not see through: x * (1 << s) would become x << s - a library call on the SH2 */
static inline int pm_opaque(int x)
{
#if defined(__GNUC__)
    __asm__("" : "+r"(x));
#endif
    return x;
}
#ifndef PM_DIV
#define PM_DIV(a, b) ((a) / (b)) /* the SH2: its on-chip divider (plat_mars.cpp) - the library's division is slow */
#endif
#ifndef PM_SKY_ROWS2 /* the SH2: the floor sampled on every other line, each line shown twice (the PC check: every line) */
#define PM_SKY_ROWS2 0
#endif
#ifndef PM_SKY_FLIPY
#define PM_SKY_FLIPY 0xF0 /* Draw3DSkyLayer's flipped-row base: SCREEN_YSIZE in the engine - the original's 240 is the \
                            flipped tile's last row (16 x 15); with this port's 224 lines it would be a row off */
#endif

#ifndef PM_TILE_MAP_OWN /* (the includer's own: plat_mars.cpp - the Genesis planes use it too) */
static unsigned short pmTileMap[TILE_COUNT]; /* the tile whose pixels tile t shows (Copy16x16Tile) */
#endif
static bool pmSkip = false;                 /* this frame's pixels are not drawn (frame skip) */
static int pmFadeR = 0, pmFadeG = 0, pmFadeB = 0, pmFadeA = 0; /* this frame's fade (a whole-screen rectangle) */
static unsigned int pmPalGen = 1;            /* the palette's version: the nearest-colour cache is for it */
/* a whole-screen rectangle with alpha waits: drawn (dithered) if anything is drawn after it, the frame's fade (the
   colours) if it is the frame's last - the engine's fade is DrawStageGFX's last call */
static bool pmPend = false;
static int pmPendR = 0, pmPendG = 0, pmPendB = 0, pmPendA = 0;
static void pm_pending_draw();
#define PM_FLUSH()             \
    do {                       \
        if (pmPend)            \
            pm_pending_draw(); \
    } while (0)

static const unsigned char pmBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
static inline int pm_alpha16(int alpha) { return (alpha + 8) >> 4; } /* 0 - 16: a pixel shows where Bayer < it */

static void pm_tiles_reset()
{
    for (int i = 0; i < TILE_COUNT; ++i) pmTileMap[i] = (unsigned short)i;
}
static inline void pm_copy_tile(int dest, int src) /* Copy16x16Tile */
{
    if ((unsigned)dest < TILE_COUNT && (unsigned)src < TILE_COUNT)
        pmTileMap[dest] = pmTileMap[src];
}

/* the palette index nearest an RGB colour (rectangles and faces have RGB colours; 256 colours have no others): the
   engine's 16-bit colour of it (RGB888_TO_RGB565) where the palette has it, else the nearest - never index 0 (see the
   top: a computed colour goes out through the overwrite image) */
struct PmNear {
    unsigned int key, gen;
    unsigned char index;
    bool exact;
};
static PmNear pmNear[8];
static int pmNearNext   = 0;
static bool pmNearExact = true; /* the last pm_nearest found the colour itself */
static int pm_nearest(int r, int g, int b)
{
    unsigned int key = (unsigned int)(r & 0xFF) << 16 | (unsigned int)(g & 0xFF) << 8 | (unsigned int)(b & 0xFF);
    for (int i = 0; i < 8; ++i)
        if (pmNear[i].gen == pmPalGen && pmNear[i].key == key) {
            pmNearExact = pmNear[i].exact;
            return pmNear[i].index;
        }
    unsigned short want = (unsigned short)(((b & 0xFF) >> 3) | (((g & 0xFF) >> 2) << 5) | (((r & 0xFF) >> 3) << 11));
    int wr = (want >> 8) & 0xF8, wg = (want >> 3) & 0xFC, wb = (want << 3) & 0xF8;
    int best = 1, bestD = 0x7FFFFFFF;
    for (int i = 1; i < 256; ++i) {
        unsigned short c = activePalette[i];
        if (c == want) {
            best  = i;
            bestD = 0;
            break;
        }
        int dr = (int)((c >> 8) & 0xF8) - wr, dg = (int)((c >> 3) & 0xFC) - wg, db = (int)((c << 3) & 0xF8) - wb;
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bestD) {
            bestD = d;
            best  = i;
        }
    }
    pmNear[pmNearNext].key   = key;
    pmNear[pmNearNext].gen   = pmPalGen;
    pmNear[pmNearNext].index = (unsigned char)best;
    pmNear[pmNearNext].exact = bestD == 0;
    pmNearNext               = (pmNearNext + 1) & 7;
    pmNearExact              = bestD == 0;
    return best;
}

/* ---------------------------------------------------------------- spans: words only */
/* n pixels at off from next() (0 = see-through): pairs as overwrite-image words, a single pixel at either end alone */
template <typename F> static inline void pm_run(unsigned int off, int n, F next)
{
    if (n <= 0)
        return;
    if (off & 1) {
        unsigned char a = next();
        if (a)
            PM_O8(off, a);
        ++off;
        --n;
    }
    for (; n >= 2; n -= 2, off += 2) {
        unsigned char a  = next();
        unsigned char b  = next();
        unsigned short w = (unsigned short)(a << 8 | b);
        if (w)
            PM_O16(off, w);
    }
    if (n) {
        unsigned char a = next();
        if (a)
            PM_O8(off, a);
    }
}
static inline void pm_fill_span(unsigned int off, int n, unsigned char c) /* n pixels of colour c (not 0) */
{
    if (n <= 0)
        return;
    if (off & 1) {
        PM_O8(off, c);
        ++off;
        --n;
    }
    unsigned short w = (unsigned short)(c << 8 | c);
    for (; n >= 2; n -= 2, off += 2) PM_W16(off, w);
    if (n)
        PM_O8(off, c);
}
static inline void pm_dither_span(unsigned int off, int x, int y, int n, unsigned char c, int a16) /* c where Bayer < a16 */
{
    const unsigned char *row = pmBayer[y & 3];
    int i                    = 0;
    pm_run(off, n, [&]() -> unsigned char { return row[(x + i++) & 3] < a16 ? c : 0; });
    PM_APPROX(off, n);
}
/* a sprite row: n pixels from s, stepping dir (+1 / -1). Where the source's pairs line up with the picture's words
   (the same parity: half the rows of an unflipped sprite, as many of a flipped one) one 16-bit read gives two pixels */
static inline void pm_sprite_row(unsigned int off, const unsigned char *s, int n, int dir)
{
    if (n <= 0)
        return;
    if (off & 1) {
        if (*s)
            PM_O8(off, *s);
        s += dir;
        ++off;
        --n;
    }
    if (dir > 0) {
        if (!((unsigned long)s & 1)) {
            for (; n >= 4; n -= 4, off += 4, s += 4) {
                unsigned short a = PM_BE16(s), b = PM_BE16(s + 2);
                if (a)
                    PM_O16(off, a);
                if (b)
                    PM_O16(off + 2, b);
            }
        }
        for (; n >= 2; n -= 2, off += 2, s += 2) {
            unsigned short w = (unsigned short)(s[0] << 8 | s[1]);
            if (w)
                PM_O16(off, w);
        }
    }
    else {
        if ((unsigned long)s & 1) { /* the pair s[-1], s[0] is a word, its bytes the other way round */
            for (; n >= 2; n -= 2, off += 2, s -= 2) {
                unsigned short v = PM_BE16(s - 1);
                unsigned short w = (unsigned short)(v << 8 | v >> 8);
                if (w)
                    PM_O16(off, w);
            }
        }
        for (; n >= 2; n -= 2, off += 2, s -= 2) {
            unsigned short w = (unsigned short)(s[0] << 8 | s[-1]);
            if (w)
                PM_O16(off, w);
        }
    }
    if (n && *s)
        PM_O8(off, *s);
}
static inline void pm_sprite_row_dither(unsigned int off, int x, int y, const unsigned char *s, int n, int dir, int a16)
{
    const unsigned char *row = pmBayer[y & 3];
    int i                    = 0;
    pm_run(off, n, [&]() -> unsigned char {
        unsigned char v = *s;
        s += dir;
        const bool on = row[(x + i++) & 3] < a16; /* REV R4D: every pixel moves the pattern on (R4C: only a sprite's
                                                     visible ones - after a see-through pixel the pattern slipped) */
        return (v && on) ? v : 0;
    });
    PM_APPROX(off, n);
}
/* REV R4D: a sprite row at half alpha (a16 8: DrawBlendedSprite - the menus' background, two whole-screen sprites a
   frame). The Bayer pattern below 8 is a checkerboard: a pixel shows where its x and y are both even or both odd, so
   one pixel of each word - one byte read and one word written a pair. The same pixels as pm_sprite_row_dither (x and
   off have the same parity: lines are PM_STRIDE, even, apart) */
static inline void pm_sprite_row_half(unsigned int off, int y, const unsigned char *s, int n, int dir)
{
    if (n <= 0)
        return;
    const int k0           = (int)((off ^ (unsigned int)y) & 1); /* the row's first pixel that shows: 0 or 1 */
    const unsigned char *p = s + k0 * dir;
    unsigned int o         = off + (unsigned int)k0;
    int cnt                = (n - k0 + 1) >> 1;
    const int step         = dir + dir;
    if (o & 1) { /* odd lines: the low byte of each word */
        for (o -= 1; cnt > 0; --cnt, o += 2, p += step) {
            const unsigned char v = *p;
            if (v)
                PM_O16(o, v);
        }
    }
    else {
        for (; cnt > 0; --cnt, o += 2, p += step) {
            const unsigned char v = *p;
            if (v)
                PM_O16(o, (unsigned short)(v << 8));
        }
    }
    PM_APPROX(off, n);
}

/* ---------------------------------------------------------------- the engine's draw calls */
static void pm_clear(int index) /* ClearScreen */
{
    PM_FLUSH();
    if (pmSkip)
        return;
    unsigned short w = (unsigned short)((index & 0xFF) << 8 | (index & 0xFF));
    for (unsigned int o = 0; o < PM_STRIDE * PM_LINES; o += 2) PM_W16(o, w);
}
/* DrawSprite / DrawSpriteFlipped (dir: FLIP_NONE .. FLIP_XY), and the blended kinds (alpha < 255: dithered) */
/* REV R4D: a sprite's rows (part of parts: every parts-th row from its top) */
struct PmSpriteJob {
    const unsigned char *g;
    int rowStep, dx, width, height, x, y, a16;
    unsigned int off;
};
static void pm_sprite_part(const PmSpriteJob *j, int part, int parts)
{
    const unsigned char *g = j->g + part * j->rowStep;
    unsigned int off       = j->off + (unsigned int)(part * PM_STRIDE);
    const int step = parts * j->rowStep, offStep = parts * PM_STRIDE, width = j->width, dx = j->dx, a16 = j->a16;
    if (a16 < 0) {
        for (int r = part; r < j->height; r += parts, g += step, off += (unsigned int)offStep) pm_sprite_row(off, g, width, dx);
    }
    else if (a16 == 8) { /* REV R4D: half alpha (above) */
        for (int r = part; r < j->height; r += parts, g += step, off += (unsigned int)offStep)
            pm_sprite_row_half(off, j->y + r, g, width, dx);
    }
    else {
        for (int r = part; r < j->height; r += parts, g += step, off += (unsigned int)offStep)
            pm_sprite_row_dither(off, j->x, j->y + r, g, width, dx, a16);
    }
}
PM_SPLIT_FN(pm_sprite_part, PmSpriteJob)
static void pm_sprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int direction, int sheetID, int alpha)
{
    PM_FLUSH();
    if (pmSkip)
        return;
    int widthFlip = width, heightFlip = height;
    if (width + XPos > PM_STRIDE)
        width = PM_STRIDE - XPos;
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        widthFlip += XPos + XPos;
        XPos = 0;
    }
    if (height + YPos > PM_LINES)
        height = PM_LINES - YPos;
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        heightFlip += YPos + YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0 || alpha <= 0)
        return;
    if (alpha > 0xFF)
        alpha = 0xFF;
    GFXSurface *surface = &gfxSurface[sheetID];
    const unsigned char *g;
    int dx, rowStep;
    switch (direction) {
        default:
        case FLIP_NONE:
            g       = &PM_GFX[sprX + surface->width * sprY + surface->dataPosition];
            dx      = 1;
            rowStep = surface->width;
            break;
        case FLIP_X:
            g       = &PM_GFX[widthFlip - 1 + sprX + surface->width * sprY + surface->dataPosition];
            dx      = -1;
            rowStep = surface->width;
            break;
        case FLIP_Y:
            g       = &PM_GFX[sprX + surface->width * (sprY + heightFlip - 1) + surface->dataPosition];
            dx      = 1;
            rowStep = -surface->width;
            break;
        case FLIP_XY:
            g       = &PM_GFX[widthFlip - 1 + sprX + surface->width * (sprY + heightFlip - 1) + surface->dataPosition];
            dx      = -1;
            rowStep = -surface->width;
            break;
    }
    PmSpriteJob j;
    j.g       = g;
    j.rowStep = rowStep;
    j.dx      = dx;
    j.off     = (unsigned int)(XPos + PM_STRIDE * YPos);
    j.width   = width;
    j.height  = height;
    j.x       = XPos;
    j.y       = YPos;
    j.a16     = alpha >= 0xFF ? -1 : pm_alpha16(alpha); /* (-1: no alpha) */
#ifndef PM_CHECKING
    if (j.a16 == 0)
        return; /* (less than 1/32 of the sprite: none of it; the PC check marks the place as approximated) */
#endif
    if (height >= PM_SPLIT_MIN && width * height >= PM_SPLIT_PIXELS)
        PM_SPLIT(pm_sprite_part, &j); /* REV R4D: a big sprite (the menus' pictures) shared with the second SH2 */
    else
        pm_sprite_part(&j, 0, 1);
}
/* the engine's float steps, exactly - (int)(float)((float)(num / (float)d) * num), the last one kept (calls in a row
   share their scale) */
static int pm_fstep(float num, int d, int *lastD, int *lastV)
{
    if (d != *lastD) {
        *lastD = d;
        *lastV = (signed int)(float)((float)(num / (float)d) * num);
    }
    return *lastV;
}
/* DrawSpriteScaled (also the bitmap text's characters) */
static void pm_sprite_scaled(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX,
                             int sprY, int sheetID)
{
    PM_FLUSH();
    if (pmSkip)
        return;
    static int lastSX = 0, lastVX = 0, lastSY = 0, lastVY = 0;
    int roundedYPos = 0, roundedXPos = 0;
    int truescaleX = 4 * scaleX, truescaleY = 4 * scaleY;
    if (truescaleX == 0 || truescaleY == 0)
        return;
    int widthM1     = width - 1;
    int trueXPos    = XPos - (truescaleX * pivotX >> 11);
    width           = truescaleX * width >> 11;
    int trueYPos    = YPos - (truescaleY * pivotY >> 11);
    height          = truescaleY * height >> 11;
    int finalscaleX = pm_fstep(2048.0f, truescaleX, &lastSX, &lastVX);
    int finalscaleY = pm_fstep(2048.0f, truescaleY, &lastSY, &lastVY);
    if (width + trueXPos > PM_STRIDE)
        width = PM_STRIDE - trueXPos;
    if (direction) {
        if (trueXPos < 0) {
            widthM1 -= trueXPos * -finalscaleX >> 11;
            roundedXPos = (unsigned short)trueXPos * -(short)finalscaleX & 0x7FF;
            width += trueXPos;
            trueXPos = 0;
        }
    }
    else if (trueXPos < 0) {
        sprX += trueXPos * -finalscaleX >> 11;
        roundedXPos = (unsigned short)trueXPos * -(short)finalscaleX & 0x7FF;
        width += trueXPos;
        trueXPos = 0;
    }
    if (height + trueYPos > PM_LINES)
        height = PM_LINES - trueYPos;
    if (trueYPos < 0) {
        sprY += trueYPos * -finalscaleY >> 11;
        roundedYPos = (unsigned short)trueYPos * -(short)finalscaleY & 0x7FF;
        height += trueYPos;
        trueYPos = 0;
    }
    if (width <= 0 || height <= 0)
        return;
    GFXSurface *surface      = &gfxSurface[sheetID];
    int gfxwidth             = surface->width;
    const unsigned char *gfx = &PM_GFX[sprX + surface->width * sprY + surface->dataPosition];
    unsigned int off         = (unsigned int)(trueXPos + PM_STRIDE * trueYPos);
    const unsigned char *p   = direction == FLIP_X ? &gfx[widthM1] : gfx;
    int step                 = direction == FLIP_X ? -1 : 1;
    while (height--) {
        int roundXPos          = roundedXPos;
        const unsigned char *q = p;
        if (finalscaleX >= 0) /* (the usual case: unsigned shifts - no library calls on the SH2) */
            pm_run(off, width, [&]() -> unsigned char {
                unsigned char v      = *q;
                unsigned int offsetX = (unsigned int)(finalscaleX + roundXPos);
                q += step * (int)(offsetX >> 11);
                roundXPos = (int)(offsetX & 0x7FF);
                return v;
            });
        else
            pm_run(off, width, [&]() -> unsigned char {
                unsigned char v = *q;
                int offsetX     = finalscaleX + roundXPos;
                q += step * (offsetX >> 11);
                roundXPos = offsetX & 0x7FF;
                return v;
            });
        off += PM_STRIDE;
        int offsetY = finalscaleY + roundedYPos;
        p += (offsetY >> 11) * gfxwidth;
        roundedYPos = offsetY & 0x7FF;
    }
}
/* DrawSpriteRotated (zoom false) and DrawSpriteRotozoom (zoom true, its scale) */
static void pm_sprite_rotozoom(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation,
                               int scale, bool zoom, int sheetID)
{
    PM_FLUSH();
    if (pmSkip || scale == 0)
        return;
    static int lastS = 0, lastV = 0;
    int sprXPos    = (pivotX + sprX) << 9;
    int sprYPos    = (pivotY + sprY) << 9;
    int fullwidth  = width + sprX;
    int fullheight = height + sprY;
    int angle      = rotation & 0x1FF;
    if (angle < 0)
        angle += 0x200;
    if (angle)
        angle = 0x200 - angle;
    int sine   = zoom ? scale * sin512LookupTable[angle] >> 9 : sin512LookupTable[angle];
    int cosine = zoom ? scale * cos512LookupTable[angle] >> 9 : cos512LookupTable[angle];
    int XPositions[4], YPositions[4];
    if (direction == FLIP_X) {
        XPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX + 2)) >> 9);
        YPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX + 2)) >> 9);
        XPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX - width - 2)) >> 9);
        YPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX - width - 2)) >> 9);
        XPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (pivotX + 2)) >> 9);
        YPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (pivotX + 2)) >> 9);
        int a = pivotX - width - 2, b = height - pivotY + 2;
        XPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
        YPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
    }
    else {
        XPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (-pivotX - 2)) >> 9);
        YPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (-pivotX - 2)) >> 9);
        XPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (width - pivotX + 2)) >> 9);
        YPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (width - pivotX + 2)) >> 9);
        XPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (-pivotX - 2)) >> 9);
        YPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (-pivotX - 2)) >> 9);
        int a = width - pivotX + 2, b = height - pivotY + 2;
        XPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
        YPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
    }
    int truescale = 0;
    if (zoom) {
        truescale = pm_fstep(512.0f, scale, &lastS, &lastV);
        sine      = truescale * sin512LookupTable[angle] >> 9;
        cosine    = truescale * cos512LookupTable[angle] >> 9;
    }
    int left = PM_STRIDE, right = 0, top = PM_LINES, bottom = 0;
    for (int i = 0; i < 4; ++i) {
        if (XPositions[i] < left)
            left = XPositions[i];
        if (XPositions[i] > right)
            right = XPositions[i];
        if (YPositions[i] < top)
            top = YPositions[i];
        if (YPositions[i] > bottom)
            bottom = YPositions[i];
    }
    if (left < 0)
        left = 0;
    if (right > PM_STRIDE)
        right = PM_STRIDE;
    if (top < 0)
        top = 0;
    if (bottom > PM_LINES)
        bottom = PM_LINES;
    int maxX = right - left, maxY = bottom - top;
    if (maxX <= 0 || maxY <= 0)
        return;
    GFXSurface *surface = &gfxSurface[sheetID];
    int rowBytes        = pm_opaque(1 << surface->widthShifted); /* (a multiply, not a shift) */
    unsigned int off    = (unsigned int)(left + PM_STRIDE * top);
    int startX          = left - XPos;
    int startY          = top - YPos;
    int shiftPivot      = (sprX << 9) - 1;
    fullwidth <<= 9;
    int shiftheight = (sprY << 9) - 1;
    fullheight <<= 9;
    const unsigned char *gfx = &PM_GFX[surface->dataPosition];
    if (cosine < 0 || sine < 0)
        sprYPos += sine + cosine;
    int drawX, drawY, sx, rowX;
    if (direction == FLIP_X) {
        drawX = sprXPos - (cosine * startX - sine * startY) - (zoom ? (truescale >> 1) : 0x100);
        drawY = cosine * startY + sprYPos + sine * startX;
        sx = -cosine, rowX = sine;
    }
    else {
        drawX = sprXPos + cosine * startX - sine * startY;
        drawY = cosine * startY + sprYPos + sine * startX;
        sx = cosine, rowX = -sine;
    }
    while (maxY--) {
        int finalX = drawX, finalY = drawY;
        pm_run(off, maxX, [&]() -> unsigned char {
            unsigned char index = 0;
            if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) /* (both >= 0 here) */
                index = gfx[((unsigned int)finalY >> 9) * (unsigned int)rowBytes + ((unsigned int)finalX >> 9)];
            finalX += sx;
            finalY += sine;
            return index;
        });
        drawX += rowX;
        drawY += cosine;
        off += PM_STRIDE;
    }
}
/* DrawRectangle: a whole-screen one with alpha may be the fade (see the top), others solid or dithered */
static void pm_rect(int XPos, int YPos, int width, int height, int R, int G, int B, int A)
{
    if (A > 0xFF)
        A = 0xFF;
    if (A <= 0)
        return;
    PM_FLUSH();
    if (A < 0xFF && XPos <= 0 && YPos <= 0 && XPos + width >= SCREEN_XSIZE && YPos + height >= PM_LINES) {
        pmPend  = true; /* the fade, unless something is drawn after it */
        pmPendR = R, pmPendG = G, pmPendB = B, pmPendA = A;
        return;
    }
    if (pmSkip)
        return;
    if (width + XPos > PM_STRIDE)
        width = PM_STRIDE - XPos;
    if (XPos < 0) {
        width += XPos;
        XPos = 0;
    }
    if (height + YPos > PM_LINES)
        height = PM_LINES - YPos;
    if (YPos < 0) {
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0)
        return;
    unsigned char c  = (unsigned char)pm_nearest(R, G, B);
    unsigned int off = (unsigned int)(XPos + PM_STRIDE * YPos);
    if (A == 0xFF)
        for (int r = 0; r < height; ++r, off += PM_STRIDE) {
            pm_fill_span(off, width, c);
            if (!pmNearExact)
                PM_APPROX(off, width);
        }
    else {
        int a16 = pm_alpha16(A);
        for (int r = 0; r < height; ++r, off += PM_STRIDE) pm_dither_span(off, XPos, YPos + r, width, c, a16);
    }
}
static void pm_pending_draw() /* the waiting whole-screen rectangle, dithered: something is drawn after it */
{
    pmPend = false;
    if (pmSkip)
        return;
    unsigned char c  = (unsigned char)pm_nearest(pmPendR, pmPendG, pmPendB);
    int a16          = pm_alpha16(pmPendA);
    unsigned int off = 0;
    for (int r = 0; r < PM_LINES; ++r, off += PM_STRIDE) pm_dither_span(off, 0, r, PM_STRIDE, c, a16);
}
static void pm_frame_end() /* the frame's fade: its last whole-screen rectangle, or none */
{
    if (pmPend) {
        pmFadeR = pmPendR, pmFadeG = pmPendG, pmFadeB = pmPendB, pmFadeA = pmPendA;
        pmPend  = false;
    }
    else
        pmFadeA = 0;
}
/* ---------------------------------------------------------------- faces (the engine's scan edges, Scene3D.cpp) */
/* ProcessScanEdge / ProcessScanEdgeUV, into PM_SCAN (the engine's own arrays are not in the SH2 build's SDRAM) */
#define pmLineStart (PM_SCAN)
#define pmLineEnd (PM_SCAN + PM_LINES)
#define pmLineStartU (PM_SCAN + 2 * PM_LINES)
#define pmLineEndU (PM_SCAN + 3 * PM_LINES)
#define pmLineStartV (PM_SCAN + 4 * PM_LINES)
#define pmLineEndV (PM_SCAN + 5 * PM_LINES)
static void pm_scan_edge(const Vertex *vertA, const Vertex *vertB)
{
    int bottom, top;
    if (vertA->y == vertB->y)
        return;
    if (vertA->y >= vertB->y) {
        top    = vertB->y;
        bottom = vertA->y + 1;
    }
    else {
        top    = vertA->y;
        bottom = vertB->y + 1;
    }
    if (top > PM_LINES - 1 || bottom < 0)
        return;
    if (bottom > PM_LINES)
        bottom = PM_LINES;
    int fullX       = vertA->x << 16;
    int yDifference = vertB->y - vertA->y;
    int fullY       = PM_DIV((vertB->x - vertA->x) << 16, yDifference);
    if (top < 0) {
        fullX -= top * fullY;
        top = 0;
    }
    for (int i = top; i < bottom; ++i) {
        int trueX = fullX >> 16;
        if (fullX >> 16 < pmLineStart[i])
            pmLineStart[i] = trueX;
        if (trueX > pmLineEnd[i])
            pmLineEnd[i] = trueX;
        fullX += fullY;
    }
}
static void pm_scan_edge_uv(const Vertex *vertA, const Vertex *vertB)
{
    int bottom, top, trueU, trueV;
    if (vertA->y == vertB->y)
        return;
    if (vertA->y >= vertB->y) {
        top    = vertB->y;
        bottom = vertA->y + 1;
    }
    else {
        top    = vertA->y;
        bottom = vertB->y + 1;
    }
    if (top > PM_LINES - 1 || bottom < 0)
        return;
    if (bottom > PM_LINES)
        bottom = PM_LINES;
    int fullX  = vertA->x << 16;
    int fullU  = vertA->u << 16;
    int fullV  = vertA->v << 16;
    int dy     = vertB->y - vertA->y;
    int finalX = PM_DIV((vertB->x - vertA->x) << 16, dy);
    if (vertA->u == vertB->u)
        trueU = 0;
    else
        trueU = PM_DIV((vertB->u - vertA->u) << 16, dy);
    if (vertA->v == vertB->v)
        trueV = 0;
    else
        trueV = PM_DIV((vertB->v - vertA->v) << 16, dy);
    if (top < 0) {
        fullX -= top * finalX;
        fullU -= top * trueU;
        fullV -= top * trueV;
        top = 0;
    }
    for (int i = top; i < bottom; ++i) {
        int trueX = fullX >> 16;
        if (fullX >> 16 < pmLineStart[i]) {
            pmLineStart[i]  = trueX;
            pmLineStartU[i] = fullU;
            pmLineStartV[i] = fullV;
        }
        if (trueX > pmLineEnd[i]) {
            pmLineEnd[i]  = trueX;
            pmLineEndU[i] = fullU;
            pmLineEndV[i] = fullV;
        }
        fullX += finalX;
        fullU += trueU;
        fullV += trueV;
    }
}
static void pm_face_order(Vertex *verts, int *a, int *b, int *c, int *d)
{
    int vertexA = 0, vertexB = 1, vertexC = 2, vertexD = 3;
    if (verts[1].y < verts[0].y) {
        vertexA = 1;
        vertexB = 0;
    }
    if (verts[2].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 2;
        vertexC  = temp;
    }
    if (verts[3].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 3;
        vertexD  = temp;
    }
    if (verts[vertexC].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexC;
        vertexC  = temp;
    }
    if (verts[vertexD].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexD;
        vertexD  = temp;
    }
    if (verts[vertexD].y < verts[vertexC].y) {
        int temp = vertexC;
        vertexC  = vertexD;
        vertexD  = temp;
    }
    *a = vertexA, *b = vertexB, *c = vertexC, *d = vertexD;
}
static bool pm_face_off(const Vertex *verts)
{
    if (verts[0].x < 0 && verts[1].x < 0 && verts[2].x < 0 && verts[3].x < 0)
        return true;
    if (verts[0].x > PM_STRIDE && verts[1].x > PM_STRIDE && verts[2].x > PM_STRIDE && verts[3].x > PM_STRIDE)
        return true;
    if (verts[0].y < 0 && verts[1].y < 0 && verts[2].y < 0 && verts[3].y < 0)
        return true;
    if (verts[0].y > PM_LINES && verts[1].y > PM_LINES && verts[2].y > PM_LINES && verts[3].y > PM_LINES)
        return true;
    if (verts[0].x == verts[1].x && verts[1].x == verts[2].x && verts[2].x == verts[3].x)
        return true;
    if (verts[0].y == verts[1].y && verts[1].y == verts[2].y && verts[2].y == verts[3].y)
        return true;
    return false;
}
/* REV R4D: DrawFace's rows (part of parts: every parts-th row from the top) */
struct PmFaceJob {
    int top, bottom, alpha, a16;
    unsigned char c;
    bool exact;
};
static void pm_face_part(const PmFaceJob *j, int part, int parts)
{
    const int mask = parts - 1, alpha = j->alpha, a16 = j->a16;
    const unsigned char c = j->c;
    for (int faceTop = j->top; faceTop < j->bottom; ++faceTop) {
        if (((faceTop - j->top) & mask) != part)
            continue;
        int startX = pmLineStart[faceTop], endX = pmLineEnd[faceTop];
        if (startX >= PM_STRIDE || endX <= 0)
            continue;
        if (startX < 0)
            startX = 0;
        if (endX > PM_STRIDE - 1)
            endX = PM_STRIDE - 1;
        unsigned int off = (unsigned int)(PM_STRIDE * faceTop + startX);
        if (alpha == 0xFF) {
            pm_fill_span(off, endX - startX + 1, c);
            if (!j->exact)
                PM_APPROX(off, endX - startX + 1);
        }
        else
            pm_dither_span(off, startX, faceTop, endX - startX + 1, c, a16);
    }
}
PM_SPLIT_FN(pm_face_part, PmFaceJob)
static void pm_face(void *v, unsigned int colour) /* DrawFace */
{
    PM_FLUSH();
    if (pmSkip)
        return;
    Vertex *verts = (Vertex *)v;
    int alpha     = (int)((colour & 0x7F000000) >> 23);
    if (alpha < 1)
        return;
    if (alpha > 0xFF)
        alpha = 0xFF;
    if (pm_face_off(verts))
        return;
    int vA, vB, vC, vD;
    pm_face_order(verts, &vA, &vB, &vC, &vD);
    int faceTop = verts[vA].y, faceBottom = verts[vD].y;
    if (faceTop < 0)
        faceTop = 0;
    if (faceBottom > PM_LINES)
        faceBottom = PM_LINES;
    for (int i = faceTop; i <= faceBottom && i < PM_LINES; ++i) { /* (and the row below: the scan edges look at it) */
        pmLineStart[i] = 100000;
        pmLineEnd[i]   = -100000;
    }
    pm_scan_edge(&verts[vA], &verts[vB]);
    pm_scan_edge(&verts[vA], &verts[vC]);
    pm_scan_edge(&verts[vA], &verts[vD]);
    pm_scan_edge(&verts[vB], &verts[vC]);
    pm_scan_edge(&verts[vC], &verts[vD]);
    pm_scan_edge(&verts[vB], &verts[vD]);
    PmFaceJob j;
    j.c      = (unsigned char)pm_nearest((int)((colour >> 16) & 0xFF), (int)((colour >> 8) & 0xFF), (int)(colour & 0xFF));
    j.a16    = pm_alpha16(alpha);
    j.alpha  = alpha;
    j.top    = faceTop;
    j.bottom = faceBottom;
    j.exact  = pmNearExact;
    if (faceBottom - faceTop >= PM_SPLIT_MIN)
        PM_SPLIT(pm_face_part, &j); /* REV R4D */
    else
        pm_face_part(&j, 0, 1);
}
/* a textured face's row that reads one texture row (U only, forwards): pairs of pixels, registers only */
static void pm_tex_span(unsigned int off, int n, const unsigned char *trow, unsigned int U, unsigned int dU) __attribute__((noinline));
static void pm_tex_span(unsigned int off, int n, const unsigned char *trow, unsigned int U, unsigned int dU)
{
    if (n <= 0)
        return;
    if (off & 1) {
        unsigned char a = trow[U >> 16];
        if (a)
            PM_O8(off, a);
        U += dU;
        ++off;
        --n;
    }
    for (; n >= 2; n -= 2, off += 2) {
        unsigned int a = trow[U >> 16];
        U += dU;
        unsigned int b = trow[U >> 16];
        U += dU;
        unsigned short w = (unsigned short)(a << 8 | b);
        if (w)
            PM_O16(off, w);
    }
    if (n) {
        unsigned char a = trow[U >> 16];
        if (a)
            PM_O8(off, a);
    }
}
/* REV R5D: the same pixels as pm_tex_span, for a step just under one texel a pixel (dU 0xF000 - 0x10000: the special
   stages' sky, 127 texels over 128 pixels). The texel goes up by one each pixel except once every (fraction / gap)
   pixels, where it repeats: runs of consecutive texels, each a plain copy (pm_sprite_row: two pixels a word) */
static void pm_tex_copy(unsigned int off, int n, const unsigned char *trow, unsigned int U, unsigned int dU) __attribute__((noinline));
static void pm_tex_copy(unsigned int off, int n, const unsigned char *trow, unsigned int U, unsigned int dU)
{
    const unsigned int gap = 0x10000u - dU;
    while (n > 0) {
        /* pixels 0 .. len - 1 from here: texels t, t + 1, ... (the fraction stays >= 0 while gap is taken off) */
        const unsigned int f = U & 0xFFFFu;
        int len              = gap ? (int)((unsigned int)PM_DIV((int)f, (int)gap)) + 1 : n;
        if (len > n)
            len = n;
        pm_sprite_row(off, trow + (U >> 16), len, 1);
        off += (unsigned int)len;
        n -= len;
        U += (unsigned int)len * dU;
    }
}
/* REV R4D: DrawTexturedFace's rows (part of parts: every parts-th row from the top) */
struct PmTexJob {
    const unsigned char *sheet;
    int rowBytes, top, bottom;
};
static void pm_tex_part(const PmTexJob *j, int part, int parts)
{
    const unsigned char *sheet = j->sheet;
    const int rowBytes = j->rowBytes, mask = parts - 1;
    for (int faceTop = j->top; faceTop < j->bottom; ++faceTop) {
        if (((faceTop - j->top) & mask) != part)
            continue;
        int startX = pmLineStart[faceTop], endX = pmLineEnd[faceTop];
        int UPos = pmLineStartU[faceTop], VPos = pmLineStartV[faceTop];
        if (startX >= PM_STRIDE || endX <= 0)
            continue;
        int posDifference = endX - startX, bufferedUPos = 0, bufferedVPos = 0;
        if (endX != startX) {
            bufferedUPos = PM_DIV(pmLineEndU[faceTop] - UPos, posDifference);
            bufferedVPos = PM_DIV(pmLineEndV[faceTop] - VPos, posDifference);
        }
        if (endX > PM_STRIDE - 1)
            posDifference = PM_STRIDE - 1 - startX;
        if (startX < 0) {
            posDifference += startX;
            UPos -= startX * bufferedUPos;
            VPos -= startX * bufferedVPos;
            startX = 0;
        }
        unsigned int off = (unsigned int)(PM_STRIDE * faceTop + startX);
        int count        = posDifference + 1;
        if (bufferedVPos == 0 && VPos >= 0) { /* one texture row (the special stages' background): U only */
            const unsigned char *trow = sheet + (VPos >> 16) * rowBytes;
            if (UPos >= 0 && bufferedUPos > 0xF000 && bufferedUPos <= 0x10000)
                pm_tex_copy(off, count, trow, (unsigned int)UPos, (unsigned int)bufferedUPos); /* REV R5D */
            else if (UPos >= 0 && bufferedUPos >= 0)
                pm_tex_span(off, count, trow, (unsigned int)UPos, (unsigned int)bufferedUPos);
            else
                pm_run(off, count, [&]() -> unsigned char {
                    if (UPos < 0)
                        UPos = 0;
                    unsigned char index = trow[UPos >> 16];
                    UPos += bufferedUPos;
                    return index;
                });
        }
        else
            pm_run(off, count, [&]() -> unsigned char {
                if (UPos < 0)
                    UPos = 0;
                if (VPos < 0)
                    VPos = 0;
                unsigned char index = sheet[(VPos >> 16) * rowBytes + (UPos >> 16)];
                UPos += bufferedUPos;
                VPos += bufferedVPos;
                return index;
            });
    }
}
PM_SPLIT_FN(pm_tex_part, PmTexJob)
#ifndef PM_HOOK_TEXFACE
#define PM_HOOK_TEXFACE(v, sheet) ((void)0) /* (the PC check's trace) */
#endif
static void pm_tex_face(void *v, int sheetID) /* DrawTexturedFace */
{
    PM_HOOK_TEXFACE(v, sheetID);
    PM_FLUSH();
    if (pmSkip)
        return;
    Vertex *verts = (Vertex *)v;
    if (pm_face_off(verts))
        return;
    int vA, vB, vC, vD;
    pm_face_order(verts, &vA, &vB, &vC, &vD);
    int faceTop = verts[vA].y, faceBottom = verts[vD].y;
    if (faceTop < 0)
        faceTop = 0;
    if (faceBottom > PM_LINES)
        faceBottom = PM_LINES;
    for (int i = faceTop; i <= faceBottom && i < PM_LINES; ++i) { /* (and the row below: the scan edges look at it) */
        pmLineStart[i] = 100000;
        pmLineEnd[i]   = -100000;
    }
    pm_scan_edge_uv(&verts[vA], &verts[vB]);
    pm_scan_edge_uv(&verts[vA], &verts[vC]);
    pm_scan_edge_uv(&verts[vA], &verts[vD]);
    pm_scan_edge_uv(&verts[vB], &verts[vC]);
    pm_scan_edge_uv(&verts[vC], &verts[vD]);
    pm_scan_edge_uv(&verts[vB], &verts[vD]);
    PmTexJob j;
    j.sheet    = &PM_GFX[gfxSurface[sheetID].dataPosition];
    j.rowBytes = pm_opaque(1 << gfxSurface[sheetID].widthShifted); /* (a multiply, not a shift) */
    j.top      = faceTop;
    j.bottom   = faceBottom;
    if (faceBottom - faceTop >= PM_SPLIT_MIN)
        PM_SPLIT(pm_tex_part, &j); /* REV R4D */
    else
        pm_tex_part(&j, 0, 1);
}
/* the script's Draw3DScene: the engine's TransformVertexBuffer, Sort3DDrawList (a bubble sort: stable, by depth, the
   deepest first - the same order as any stable sort) and Draw3DScene, its transformed vertices and its sorted list in
   PM_VERTS / PM_LIST (the SH2 build's own are one entry long) */
static void pm_draw3dscene(int spriteSheetID)
{
    Vertex *vt            = PM_VERTS;
    DrawListEntry3D *list = PM_LIST;
    int nv = vertexCount, nf = faceCount;
    if (nv > PM_VERTS_MAX)
        nv = PM_VERTS_MAX;
    if (nv > VERTEXBUFFER_SIZE)
        nv = VERTEXBUFFER_SIZE;
    if (nf > PM_FACES_MAX)
        nf = PM_FACES_MAX;
    if (nf > FACEBUFFER_SIZE)
        nf = FACEBUFFER_SIZE;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) matFinal.values[y][x] = matWorld.values[y][x];
    MatrixMultiply(&matFinal, &matView);
    if (pmSkip)
        return; /* (matFinal as the engine leaves it; nothing else reads the rest) */
    for (int i = 0; i < nv; ++i) {
        int vx = vertexBuffer[i].x, vy = vertexBuffer[i].y, vz = vertexBuffer[i].z;
        vt[i].x = (vx * matFinal.values[0][0] >> 8) + (vy * matFinal.values[1][0] >> 8) + (vz * matFinal.values[2][0] >> 8) + matFinal.values[3][0];
        vt[i].y = (vx * matFinal.values[0][1] >> 8) + (vy * matFinal.values[1][1] >> 8) + (vz * matFinal.values[2][1] >> 8) + matFinal.values[3][1];
        vt[i].z = (vx * matFinal.values[0][2] >> 8) + (vy * matFinal.values[1][2] >> 8) + (vz * matFinal.values[2][2] >> 8) + matFinal.values[3][2];
    }
    for (int i = 0; i < nf; ++i) { /* insertion sort: stable, deepest first */
        const Face *f = &faceBuffer[i];
        int z         = 0;
        if ((unsigned)f->a < (unsigned)nv && (unsigned)f->b < (unsigned)nv && (unsigned)f->c < (unsigned)nv && (unsigned)f->d < (unsigned)nv)
            z = (vt[f->d].z + vt[f->c].z + vt[f->b].z + vt[f->a].z) >> 2;
        int j = i;
        while (j > 0 && z > list[j - 1].depth) {
            list[j] = list[j - 1];
            --j;
        }
        list[j].depth  = z;
        list[j].faceID = i;
    }
    Vertex quad[4];
    for (int i = 0; i < nf; ++i) {
        Face *face = &faceBuffer[list[i].faceID];
        if ((unsigned)face->a >= (unsigned)nv || (unsigned)face->b >= (unsigned)nv || (unsigned)face->c >= (unsigned)nv ||
            (unsigned)face->d >= (unsigned)nv)
            continue;
        memset(quad, 0, sizeof(quad));
        const int id[4] = { face->a, face->b, face->c, face->d };
        switch (face->flags) {
            default: break;
            case FACE_FLAG_TEXTURED_3D:
            case FACE_FLAG_COLOURED_3D:
                if (vt[face->a].z > 0x100 && vt[face->b].z > 0x100 && vt[face->c].z > 0x100 && vt[face->d].z > 0x100) {
                    for (int k = 0; k < 4; ++k) {
                        const Vertex *t = &vt[id[k]];
                        quad[k].x       = SCREEN_CENTERX + PM_DIV(projectionX * t->x, t->z);
                        quad[k].y       = SCREEN_CENTERY - PM_DIV(projectionY * t->y, t->z);
                        if (face->flags == FACE_FLAG_TEXTURED_3D) {
                            quad[k].u = vertexBuffer[id[k]].u;
                            quad[k].v = vertexBuffer[id[k]].v;
                        }
                    }
                    if (face->flags == FACE_FLAG_TEXTURED_3D)
                        pm_tex_face(quad, spriteSheetID);
                    else
                        pm_face(quad, (unsigned int)face->colour);
                }
                break;
            case FACE_FLAG_TEXTURED_2D:
            case FACE_FLAG_COLOURED_2D:
                for (int k = 0; k < 4; ++k) {
                    quad[k].x = vertexBuffer[id[k]].x;
                    quad[k].y = vertexBuffer[id[k]].y;
                    if (face->flags == FACE_FLAG_TEXTURED_2D) {
                        quad[k].u = vertexBuffer[id[k]].u;
                        quad[k].v = vertexBuffer[id[k]].v;
                    }
                }
                if (face->flags == FACE_FLAG_TEXTURED_2D)
                    pm_tex_face(quad, spriteSheetID);
                else
                    pm_face(quad, (unsigned int)face->colour);
                break;
        }
    }
}

/* ---------------------------------------------------------------- tile layers */
static inline const unsigned char *pm_tile_row(int entry, int ty16, int *dx) /* the tile row's first pixel, x step */
{
    const unsigned char *t = PM_TILES + ((unsigned int)pmTileMap[PM_CHUNK_TILE(entry) & (TILE_COUNT - 1)] << 8);
    switch (PM_CHUNK_DIR(entry)) {
        default:
        case FLIP_NONE: *dx = 1; return t + 16 * ty16;
        case FLIP_X: *dx = -1; return t + 16 * ty16 + 15;
        case FLIP_Y: *dx = 1; return t + 16 * (15 - ty16);
        case FLIP_XY: *dx = -1; return t + 16 * (15 - ty16) + 15;
    }
}
/* REV R4D: DrawHLineScrollLayer's lines (part of parts: every parts-th line) */
struct PmHlineJob {
    const TileLayer *layer;
    int fullW, fullH, plane;
    unsigned int ly0;
    const int *lines;
    int ylim; /* REV R5B: the lines drawn (the chip's floor covers the rest: PM_HLINE_LINES) */
    int y0;   /* REV R5G: ... from this one (the Genesis's sky above it) */
};
static void pm_hline_part(const PmHlineJob *j, int part, int parts)
{
    const TileLayer *layer = j->layer;
    const int fullW = j->fullW, fullH = j->fullH, plane = j->plane, mask = parts - 1;
    const int *lines = j->lines;
    unsigned int ly = j->ly0 + (unsigned int)j->y0, uW = (unsigned int)fullW; /* (unsigned: no library shifts on the SH2) */
    const int ylim = j->ylim;
    while (ly >= (unsigned int)fullH) ly -= (unsigned int)fullH;
    for (int y = j->y0; y < ylim; ++y, ++ly) {
        if (ly >= (unsigned int)fullH)
            ly -= (unsigned int)fullH;
        if ((y & mask) != part)
            continue;
        unsigned int chunkY = ly >> 7, tileY = (ly & 0x7F) >> 4, ty16 = ly & 15;
        int lx = lines[y];
        if (lx < 0 || lx >= fullW) /* (the engine's arithmetic keeps it inside the layer; just in case) */
            lx = ((lx % fullW) + fullW) % fullW;
        unsigned int cx           = (unsigned int)lx;
        unsigned int off          = (unsigned int)(PM_STRIDE * y);
        const unsigned short *row = &layer->tiles[chunkY << 8];
        int x                     = 0, chunkBase = 0;
        unsigned int chunkCol     = ~0u;
        while (x < PM_STRIDE) {
            int px = (int)(cx & 15), n = 16 - px;
            if (n > PM_STRIDE - x)
                n = PM_STRIDE - x;
            if ((cx >> 7) != chunkCol) { /* (the layout is in the frame buffer: one read per chunk, not per tile) */
                chunkCol  = cx >> 7;
                chunkBase = (row[chunkCol] << 6) + 8 * (int)tileY;
            }
            int entry = chunkBase + (int)((cx & 0x7F) >> 4);
            if (PM_CHUNK_PLANE(entry) == plane) {
                int dx;
                const unsigned char *s = pm_tile_row(entry, (int)ty16, &dx);
                /* REV R5D: a tile row with no pixel (the sky layers are mostly see-through): nothing to write - its 16
                   bytes as four words (tile rows are 16-byte aligned in the image's tiles) */
                const unsigned int *w4 = (const unsigned int *)(dx > 0 ? s : s - 15);
                if (w4[0] | w4[1] | w4[2] | w4[3])
                    pm_sprite_row(off + x, s + px * dx, n, dx);
            }
            x += n;
            cx += (unsigned int)n;
            if (cx >= uW)
                cx -= uW;
        }
    }
}
PM_SPLIT_FN(pm_hline_part, PmHlineJob)
/* DrawHLineScrollLayer's pixels: lines[] holds each line's x (the engine's own scroll arithmetic), tileYPos the layer
   row at the screen's top - every line of the layer, its tiles of this half (visualPlane == aboveMidPoint) */
static void pm_hline_from(int layerID, int tileYPos, const int *lines, int y0) /* REV R5G: from line y0 */
{
    PM_FLUSH();
    if (pmSkip)
        return;
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    int fullW = layer->xsize << 7, fullH = layer->ysize << 7;
    if (fullW <= 0 || fullH <= 0)
        return;
    PmHlineJob j;
    j.layer = layer;
    j.fullW = fullW;
    j.fullH = fullH;
    j.plane = layerID >= tLayerMidPoint ? 1 : 0;
    j.ly0   = (unsigned int)tileYPos;
    j.lines = lines;
#ifdef PM_HLINE_LINES
    j.ylim = PM_HLINE_LINES; /* REV R5B: the special stages' chip floor - the lines above it only */
#else
    j.ylim = PM_LINES;
#endif
    j.y0 = y0;
    if (j.y0 >= j.ylim)
        return;
    PM_SPLIT(pm_hline_part, &j); /* REV R4D */
}
static void pm_hline(int layerID, int tileYPos, const int *lines) { pm_hline_from(layerID, tileYPos, lines, 0); }
/* Draw3DSkyLayer (the special stages' floor): rows (SCREEN_YSIZE / 2) + 12 on, 100 of them; the engine samples each
   pixel four times (two rows of samples, two samples a pixel, the later visible one wins) - here the last sample first,
   the others only where it is see-through */
struct PmSkyCache {
    unsigned int tx, ty;       /* the 16 x 16 tile (X >> 16, Y >> 16) the entries below are for (~0: none) */
    const unsigned char *base; /* its pixels */
    unsigned int xm, ym;       /* its flip: the column / row within it XOR 15 */
#if PM_SKY_FLIPY != 0xF0
    int tile;
#endif
};
struct PmSkyRow { /* one screen row of the floor: its two rows of samples (k 0: row i, k 1: row i + 1) */
    const TileLayer *floor;
    unsigned int lw, lh;
    int Xs[2], Ys[2], dX[2], dY[2];
    PmSkyCache cc[2];
};
/* the cache missed: the tile's entry (out of line - the fast path stays small enough for the SH2's registers) */
static void pm_sky_fill(const TileLayer *floor, unsigned int tx, unsigned int ty, PmSkyCache *cc) __attribute__((noinline));
static void pm_sky_fill(const TileLayer *floor, unsigned int tx, unsigned int ty, PmSkyCache *cc)
{
    /* the engine's tile3DFloorBuffer[(Y >> 16 << 8) + (X >> 16)] - Init3DFloorBuffer(0) made it from layer 0's layout
       when the stage loaded (no script changes a special stage's layout) */
    unsigned int idx = (ty << 8) + tx;
    cc->tx = tx, cc->ty = ty;
    if (idx >= 0x10000u) { /* (outside the engine's buffer: nothing) */
        static const unsigned char none[256] = { 0 };
        cc->base = none;
        cc->xm = cc->ym = 0;
        return;
    }
    unsigned int ex = idx & 0xFF, ey = idx >> 8, row = ey >> 3;
    int c     = row < floor->ysize ? floor->tiles[(ex >> 3) + (row << 8)] : 0;
    int entry = (c << 6) + (int)(ex & 7) + (int)((ey & 7) << 3);
    int tile  = PM_CHUNK_TILE(entry) & (TILE_COUNT - 1), dir = PM_CHUNK_DIR(entry);
    cc->base  = PM_TILES + ((unsigned int)pmTileMap[tile] << 8);
    cc->xm    = (dir == FLIP_X || dir == FLIP_XY) ? 15 : 0;
    cc->ym    = (dir == FLIP_Y || dir == FLIP_XY) ? 15 : 0;
#if PM_SKY_FLIPY != 0xF0
    cc->tile = tile;
#endif
}
/* one sample: X, Y the engine's (12 bits of fraction a pixel of the layer); unsigned - a negative one is outside, as the
   engine's tileX > -1 test (and no arithmetic shifts: on the SH2 those are library calls) */
static inline __attribute__((always_inline)) unsigned char pm_sky_sample(const PmSkyRow *r, unsigned int X, unsigned int Y, PmSkyCache *cc)
{
    unsigned int tileX = X >> 12, tileY = Y >> 12;
    if (tileX >= r->lw || tileY >= r->lh)
        return 0;
    if ((tileX >> 4) != cc->tx || (tileY >> 4) != cc->ty)
        pm_sky_fill(r->floor, tileX >> 4, tileY >> 4, cc);
#if PM_SKY_FLIPY != 0xF0
    if (cc->ym) { /* (the PC check: the engine's 224-line base - a row off, the tile before it at the top) */
        int o = (int)((tileX & 15) ^ cc->xm) + PM_SKY_FLIPY - 16 * (int)(tileY & 15);
        if (o < 0)
            return PM_TILES[((unsigned int)pmTileMap[(cc->tile - 1) & (TILE_COUNT - 1)] << 8) + (unsigned int)(o + 256)];
        return cc->base[o];
    }
#endif
    return cc->base[(((tileY & 15) ^ cc->ym) << 4) | ((tileX & 15) ^ cc->xm)];
}
/* pixel p's last sample was see-through: the engine's three before it, latest first */
static unsigned char pm_sky_more(PmSkyRow *r, int p, unsigned int X1, unsigned int Y1) __attribute__((noinline));
static unsigned char pm_sky_more(PmSkyRow *r, int p, unsigned int X1, unsigned int Y1)
{
    unsigned char c = pm_sky_sample(r, X1 - (unsigned int)r->dX[1], Y1 - (unsigned int)r->dY[1], &r->cc[1]);
    if (!c) {
        int sm = 2 * p;
        c      = pm_sky_sample(r, (unsigned int)(r->Xs[0] + (sm + 1) * r->dX[0]), (unsigned int)(r->Ys[0] + (sm + 1) * r->dY[0]), &r->cc[0]);
        if (!c)
            c = pm_sky_sample(r, (unsigned int)(r->Xs[0] + sm * r->dX[0]), (unsigned int)(r->Ys[0] + sm * r->dY[0]), &r->cc[0]);
    }
    return c;
}
static void pm_sky_row(PmSkyRow *r, unsigned int off, bool twice) __attribute__((noinline));
static void pm_sky_row(PmSkyRow *r, unsigned int off, bool twice) /* off: the row's first pixel (even); twice: the line below too */
{
    unsigned int X1 = (unsigned int)(r->Xs[1] + r->dX[1]), Y1 = (unsigned int)(r->Ys[1] + r->dY[1]); /* row i + 1, sample 1 */
    unsigned int dx = 2u * (unsigned int)r->dX[1], dy = 2u * (unsigned int)r->dY[1];
    PmSkyCache *cc = &r->cc[1];
    for (int p = 0; p < PM_STRIDE; p += 2, off += 2) {
        unsigned char a = pm_sky_sample(r, X1, Y1, cc);
        if (!a)
            a = pm_sky_more(r, p, X1, Y1);
        X1 += dx, Y1 += dy;
        unsigned char b = pm_sky_sample(r, X1, Y1, cc);
        if (!b)
            b = pm_sky_more(r, p + 1, X1, Y1);
        X1 += dx, Y1 += dy;
        unsigned short w = (unsigned short)(a << 8 | b);
        if (w) {
            PM_O16(off, w);
            if (twice)
                PM_O16(off + PM_STRIDE, w);
        }
    }
}
/* REV R4D: the floor's rows (part of parts: every parts-th row it draws) */
struct PmSkyJob {
    const TileLayer *floor;
    unsigned int lw, lh;
    int layerYPos, sinValue, cosValue, layerXPos, layerZPos;
    int rowEnd; /* REV R5B: the rows drawn end here (the chip's floor from there on) */
};
static void pm_sky_part(const PmSkyJob *j, int part, int parts)
{
    PmSkyRow r;
    r.floor = j->floor;
    r.lw    = j->lw;
    r.lh    = j->lh;
    const int layerYPos = j->layerYPos, sinValue = j->sinValue, cosValue = j->cosValue, layerXPos = j->layerXPos;
    const int layerZPos = j->layerZPos, mask = parts - 1;
    int row = (PM_LINES / 2) + 12, drawn = 0;
    for (int i = TILE_SIZE / 2; i + 1 < PM_LINES - TILE_SIZE && row < j->rowEnd; i += 2, ++row) {
        if (PM_SKY_ROWS2 && ((row - (PM_LINES / 2) - 12) & 1))
            continue; /* (the line above drew this one too) */
        if ((drawn++ & mask) != part)
            continue;
        for (int k = 0; k < 2; ++k) { /* row i (k 0) and i + 1 (k 1) */
            int ii      = i + k;
            int q       = PM_DIV(layerYPos, ii << 8);
            int xBuffer = q * -cosValue >> 9;
            int yBuffer = sinValue * q >> 9;
            r.Xs[k]     = layerXPos + (3 * sinValue * q >> 2) - xBuffer * SCREEN_XSIZE;
            r.Ys[k]     = layerZPos + (3 * cosValue * q >> 2) - yBuffer * SCREEN_XSIZE;
            r.dX[k] = xBuffer, r.dY[k] = yBuffer;
            r.cc[k].tx = r.cc[k].ty = ~0u;
        }
        /* pixel p: samples 2p, 2p + 1 of rows i and i + 1; the engine writes them in the order (i, 2p), (i, 2p + 1),
           (i + 1, 2p), (i + 1, 2p + 1) - the last non-zero one shows */
        pm_sky_row(&r, (unsigned int)(PM_STRIDE * row), PM_SKY_ROWS2 && row + 1 < PM_LINES);
    }
}
PM_SPLIT_FN(pm_sky_part, PmSkyJob)
static void pm_sky_rows(int layerID, int rowEnd) /* REV R5B: the floor's rows down to rowEnd (PM_LINES: all of it) */
{
    PM_FLUSH();
    if (pmSkip)
        return;
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return;
    PmSkyJob j;
    j.rowEnd    = rowEnd;
    j.floor     = &stageLayouts[0];
    j.lw        = (unsigned int)layer->xsize << 7;
    j.lh        = (unsigned int)layer->ysize << 7;
    j.layerYPos = layer->YPos;
    j.sinValue  = sinMLookupTable[layer->angle & 0x1FF];
    j.cosValue  = cosMLookupTable[layer->angle & 0x1FF];
    j.layerXPos = layer->XPos >> 4;
    j.layerZPos = layer->ZPos >> 4;
    PM_SPLIT(pm_sky_part, &j); /* REV R4D */
}
static void pm_sky(int layerID) { pm_sky_rows(layerID, PM_LINES); }
#endif
