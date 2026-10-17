// PORT32X: the PC host build's platform layer (see engine/plat.hpp).
// No window, no sound device: frames go to PNG files and a CRC log, music and
// sound requests go to the event log, the pad comes from a script, and time is
// a virtual clock (one frame = 1/60 s), so every run is identical frame for frame.
//
// Environment variables (all optional):
//   SCD32X_MAX_FRAMES   stop after this many frames (default 600 = 10 s)
//   SCD32X_FRAMES_DIR   save frames as PNG into this folder
//   SCD32X_SAVE_EVERY   save every Nth frame (default 15)
//   SCD32X_CRC_LOG      write "frame crc" lines for every Nth frame (regression)
//   SCD32X_EVENT_LOG    music / sfx / video / load events (default: stdout)
//   SCD32X_PAD          scripted pad: t2000:S,t2100:-,t5000:R+C  (ms:buttons)
//   SCD32X_MDCHECK=1    compare the engine's tile layers with the Mega Drive plane model
//                       buttons U D L R A B C S(start), '-' = release all

#include "../engine/RetroEngine.hpp"
#include <sys/mman.h> // S5h: the read-only cart image in the SH2 configuration
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// the Mega Drive plane check (md_check.cpp)
void md_check_init();
ushort *md_layer_begin(int slot, int layer, int tileYPos, ushort *frame);
void md_layer_line(int slot, int line, int x);
void md_layer_end(int slot);
void md_check_frame();
void md_sprite_draw(int kind, int sheet, int sx, int sy, int w, int h, int x, int y, int dir, int rot, int px, int py); // REV R3
void md_layout_set(int layer, int x, int y); // REV R4B
void md_tile_copy_hook(int dest, int src);      // REV R4C: Copy16x16Tile (the act's animated tiles)
void md_tiles_new();                            // REV R4C: a stage's tiles loaded
void md_check_summary();

static unsigned int frameCount = 0;
static unsigned int maxFrames  = 600;
static unsigned int saveEvery  = 15;
static const char *framesDir   = NULL;
static FILE *crcLog            = NULL;
static FILE *eventLog          = NULL;

struct PadStep {
    unsigned int ms;
    unsigned int pad;
};
static PadStep padScript[256];
static int padSteps = 0;

// ---------------------------------------------------------------- heap counting
#ifndef SCD32X_NO_HEAPCOUNT // AddressSanitizer builds use ASan's own malloc
// Linked with -Wl,--wrap=malloc,... so every allocation of the engine is counted.
extern "C" {
void *__real_malloc(size_t n);
void __real_free(void *p);
void *__real_realloc(void *p, size_t n);
void *__real_calloc(size_t a, size_t b);
}
static size_t heapNow = 0, heapPeak = 0;
static const size_t HDR = 16;

extern "C" void *__wrap_malloc(size_t n)
{
    byte *p = (byte *)__real_malloc(n + HDR);
    if (!p)
        return NULL;
    *(size_t *)p = n;
    heapNow += n;
    if (heapNow > heapPeak)
        heapPeak = heapNow;
    return p + HDR;
}
extern "C" void __wrap_free(void *q)
{
    if (!q)
        return;
    byte *p = (byte *)q - HDR;
    heapNow -= *(size_t *)p;
    __real_free(p);
}
extern "C" void *__wrap_calloc(size_t a, size_t b)
{
    void *q = __wrap_malloc(a * b);
    if (q)
        memset(q, 0, a * b);
    return q;
}
extern "C" void *__wrap_realloc(void *q, size_t n)
{
    if (!q)
        return __wrap_malloc(n);
    size_t old = *(size_t *)((byte *)q - HDR);
    void *r    = __wrap_malloc(n);
    if (r) {
        memcpy(r, q, old < n ? old : n);
        __wrap_free(q);
    }
    return r;
}
void *operator new(size_t n) { return __wrap_malloc(n); }
void *operator new[](size_t n) { return __wrap_malloc(n); }
void operator delete(void *p) noexcept { __wrap_free(p); }
void operator delete[](void *p) noexcept { __wrap_free(p); }
void operator delete(void *p, size_t) noexcept { __wrap_free(p); }
void operator delete[](void *p, size_t) noexcept { __wrap_free(p); }
#else
static size_t heapNow = 0, heapPeak = 0;
#endif

// ---------------------------------------------------------------- small helpers
static unsigned int crcTable[256];
static void crcInit()
{
    for (unsigned int i = 0; i < 256; i++) {
        unsigned int c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[i] = c;
    }
}
static unsigned int crcUpdate(unsigned int c, const byte *p, size_t n)
{
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void ev(const char *fmt, ...)
{
    FILE *f = eventLog ? eventLog : stdout;
    va_list a;
    va_start(a, fmt);
    fprintf(f, "[%6u] ", frameCount);
    vfprintf(f, fmt, a);
    fputc('\n', f);
    va_end(a);
    fflush(f);
}

// PNG without zlib: deflate "stored" blocks + adler32.
static void put32(FILE *f, unsigned int v)
{
    byte b[4] = { (byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v };
    fwrite(b, 1, 4, f);
}
static void pngChunk(FILE *f, const char *type, const byte *data, unsigned int len)
{
    put32(f, len);
    fwrite(type, 1, 4, f);
    if (len)
        fwrite(data, 1, len, f);
    unsigned int c = crcUpdate(0xFFFFFFFFu, (const byte *)type, 4);
    c              = crcUpdate(c, data, len);
    put32(f, c ^ 0xFFFFFFFFu);
}
static void saveRawPNG(const char *path, byte *raw, size_t rawLen, int w, int h);
static void savePNG(const char *path, const ushort *frame, int w, int h, int pitch)
{
    size_t rawLen = (size_t)(w * 3 + 1) * h;
    byte *raw     = (byte *)malloc(rawLen);
    byte *r       = raw;
    for (int y = 0; y < h; y++) {
        *r++ = 0;
        for (int x = 0; x < w; x++) {
            ushort p = frame[y * pitch + x]; // RGB565
            *r++     = (byte)(((p >> 11) & 0x1F) * 255 / 31);
            *r++     = (byte)(((p >> 5) & 0x3F) * 255 / 63);
            *r++     = (byte)((p & 0x1F) * 255 / 31);
        }
    }
    saveRawPNG(path, raw, rawLen, w, h);
    free(raw);
}
static void saveRawPNG(const char *path, byte *raw, size_t rawLen, int w, int h)
{
    size_t blocks = (rawLen + 65534) / 65535;
    size_t zLen   = 2 + rawLen + blocks * 5 + 4;
    byte *z       = (byte *)malloc(zLen);
    byte *o       = z;
    *o++          = 0x78;
    *o++          = 0x01;
    unsigned int a = 1, b = 0;
    for (size_t i = 0; i < rawLen; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < rawLen;) {
        size_t n = rawLen - pos > 65535 ? 65535 : rawLen - pos;
        *o++     = pos + n == rawLen ? 1 : 0;
        *o++     = (byte)n;
        *o++     = (byte)(n >> 8);
        *o++     = (byte)~n;
        *o++     = (byte)(~n >> 8);
        memcpy(o, raw + pos, n);
        o += n;
        pos += n;
    }
    unsigned int ad = (b << 16) | a;
    *o++            = (byte)(ad >> 24);
    *o++            = (byte)(ad >> 16);
    *o++            = (byte)(ad >> 8);
    *o++            = (byte)ad;

    FILE *f = fopen(path, "wb");
    if (f) {
        static const byte sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
        fwrite(sig, 1, 8, f);
        byte ihdr[13] = { (byte)(w >> 24), (byte)(w >> 16), (byte)(w >> 8), (byte)w, (byte)(h >> 24), (byte)(h >> 16), (byte)(h >> 8), (byte)h,
                          8, 2, 0, 0, 0 };
        pngChunk(f, "IHDR", ihdr, 13);
        pngChunk(f, "IDAT", z, (unsigned int)(o - z));
        pngChunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(z);
}

static unsigned int parseButtons(const char *s, const char *end)
{
    unsigned int pad = 0;
    for (; s < end; s++) {
        switch (*s) {
            case 'U': pad |= PAD_UP; break;
            case 'D': pad |= PAD_DOWN; break;
            case 'L': pad |= PAD_LEFT; break;
            case 'R': pad |= PAD_RIGHT; break;
            case 'A': pad |= PAD_A; break;
            case 'B': pad |= PAD_B; break;
            case 'C': pad |= PAD_C; break;
            case 'S': pad |= PAD_START; break;
            default: break; // '-', '+'
        }
    }
    return pad;
}

static void hostInit()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    crcInit();
    const char *v;
    if ((v = getenv("SCD32X_MAX_FRAMES")))
        maxFrames = (unsigned int)atoi(v);
    if ((v = getenv("SCD32X_SAVE_EVERY")) && atoi(v) > 0)
        saveEvery = (unsigned int)atoi(v);
    framesDir = getenv("SCD32X_FRAMES_DIR");
    if ((v = getenv("SCD32X_CRC_LOG")))
        crcLog = fopen(v, "w");
    if ((v = getenv("SCD32X_EVENT_LOG")))
        eventLog = fopen(v, "w");
    md_check_init();
    if ((v = getenv("SCD32X_PAD"))) {
        while (*v && padSteps < 256) {
            if (*v == 't')
                v++;
            unsigned int ms = (unsigned int)strtoul(v, (char **)&v, 10);
            if (*v != ':')
                break;
            v++;
            const char *e = strchr(v, ',');
            if (!e)
                e = v + strlen(v);
            padScript[padSteps].ms  = ms;
            padScript[padSteps].pad = parseButtons(v, e);
            padSteps++;
            v = *e ? e + 1 : e;
        }
    }
}

// ---------------------------------------------------------------- the Mega Drive plane check (md_check.cpp)

void md_log(const char *fmt, ...)
{
    FILE *f = eventLog ? eventLog : stdout;
    va_list a;
    va_start(a, fmt);
    fprintf(f, "[%6u] ", frameCount);
    vfprintf(f, fmt, a);
    fputc('\n', f);
    va_end(a);
    fflush(f);
}
const char *md_frames_dir() { return framesDir; }
unsigned int md_frame_number() { return frameCount; }
void md_save_png_path(const char *path, const unsigned char *rgb, int w, int h) // REV R4B: any path
{
    size_t rawLen = (size_t)(w * 3 + 1) * h;
    byte *raw     = (byte *)malloc(rawLen);
    for (int y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;
        memcpy(raw + y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    saveRawPNG(path, raw, rawLen, w, h);
    free(raw);
}
void md_save_png(const char *name, const unsigned char *rgb, int w, int h)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", framesDir, name);
    size_t rawLen = (size_t)(w * 3 + 1) * h;
    byte *raw     = (byte *)malloc(rawLen);
    for (int y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;
        memcpy(raw + y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    saveRawPNG(path, raw, rawLen, w, h);
    free(raw);
}

static int pmLayerY_[4];
static bool pmLayerOn_[4];
static int pmLines_[224];
void pm_layer_end_host(int slot, const int *lines);
unsigned short *plat_layer_begin(int slot, int layer, int tileYPos, unsigned short *frame)
{
    hostInit();
    if (scdPicture && slot >= 0 && slot < 4) { // REV R4C: the 32X picture mode's layer (its lines below)
        pmLayerY_[slot]  = tileYPos;
        pmLayerOn_[slot] = true;
    }
    return md_layer_begin(slot, layer, tileYPos, frame);
}
void plat_layer_line(int slot, int line, int x)
{
    if (scdPicture && line >= 0 && line < 224)
        pmLines_[line] = x; // REV R4C
    md_layer_line(slot, line, x);
}
// R1T: the no-draw build's lines come as one buffer per slot; the check gets them line by line at the end,
// as the per-line calls would have given them
static int hostLines[4][SCREEN_YSIZE];
static bool hostLinesOut[4];
int *plat_layer_lines(int slot)
{
    if (slot < 0 || slot >= 4)
        return NULL;
    hostLinesOut[slot] = true;
    return hostLines[slot];
}
void plat_layer_end(int slot)
{
    if (scdPicture && slot >= 0 && slot < 4 && pmLayerOn_[slot]) { // REV R4C: the layer's pixels in the picture mode
        pmLayerOn_[slot] = false;
        pm_layer_end_host(slot, hostLinesOut[slot] ? hostLines[slot] : pmLines_);
    }
    if (slot >= 0 && slot < 4 && hostLinesOut[slot]) {
        hostLinesOut[slot] = false;
        for (int line = 0; line < SCREEN_YSIZE; ++line) md_layer_line(slot, line, hostLines[slot][line]);
    }
    md_layer_end(slot);
}

// ---------------------------------------------------------------- plat.hpp
unsigned int plat_ticks_ms() { return frameCount * 1000 / 60; }

static void draw_log_frame(); // S7e (below)
static void menu_log(); // REV R4
// REV R4B: SCD32X_POKE="frame:Global.Name=value,frame:Name=value,..." - a test sets the game's global variables at a
// frame (e.g. a time warp: Warp.Destination and Warp.Timer), logged as POKE
static void poke_frame()
{
    static const char *pk = getenv("SCD32X_PEEKG"); // "first:last:Name,Name": those globals logged each frame first-last
    if (pk && *pk) {
        const char *p    = pk;
        unsigned int a = (unsigned int)strtoul(p, (char **)&p, 10), b = (unsigned int)strtoul(p + 1, (char **)&p, 10);
        if (frameCount >= a && frameCount <= b && *p == ':') {
            char line[512];
            int len = 0;
            for (++p; *p;) {
                const char *e = strchr(p, ',');
                size_t n      = e ? (size_t)(e - p) : strlen(p);
                for (int g = 0; g < globalVariablesCount; ++g)
                    if (strlen(globalVariableNames[g]) == n && !strncmp(globalVariableNames[g], p, n))
                        len += snprintf(line + len, sizeof(line) - len, " %.*s=%d", (int)n, p, globalVariables[g]);
                p += n;
                if (*p == ',')
                    p++;
            }
            ev("PEEKG%s stageMode %d list %d pos %d", line, stageMode, activeStageList, stageListPosition);
        }
    }
    static const char *lo = getenv("SCD32X_LISTOBJ"); // "first[-last]:type": the entities of that type (slot, place, state)
    if (lo && *lo && strchr(lo, ':')) {
        const char *q  = lo;
        unsigned int a = (unsigned int)strtoul(q, (char **)&q, 10), b = a;
        if (*q == '-')
            b = (unsigned int)strtoul(q + 1, (char **)&q, 10);
        if (frameCount >= a && frameCount <= b) {
            int type = atoi(strchr(lo, ':') + 1);
            if (type == 9998) { // every type in use: its number, name and count
                int count[OBJECT_COUNT] = { 0 };
                for (int e = 0; e < ENTITY_COUNT; ++e) count[objectEntityList[e].type]++;
                for (int t = 1; t < OBJECT_COUNT; ++t)
                    if (count[t])
                        ev("TYPE %d %s x%d", t, typeNames[t], count[t]);
            }
            for (int e = 0; e < ENTITY_COUNT; ++e)
                if (objectEntityList[e].type == type) {
                    Entity &o = objectEntityList[e];
                    ev("OBJ slot %d type %d x %d y %d prop %d state %d prio %d draw %d v %d %d %d %d %d %d %d %d", e, type, o.XPos >> 16,
                       o.YPos >> 16, o.propertyValue, o.state, o.priority, o.drawOrder, o.values[0], o.values[1], o.values[2], o.values[3],
                       o.values[4], o.values[5], o.values[6], o.values[7]);
                }
            ev("PLAYER x %d y %d stageMode %d layers %d %d %d %d scroll %d %d camera target %d enabled %d drawlist3 %d [%d %d %d]",
               playerList[0].XPos >> 16, playerList[0].YPos >> 16, stageMode, activeTileLayers[0], activeTileLayers[1], activeTileLayers[2],
               activeTileLayers[3], xScrollOffset, yScrollOffset, cameraTarget, cameraEnabled, drawListEntries[3].listSize,
               drawListEntries[3].entityRefs[0], drawListEntries[3].entityRefs[1], drawListEntries[3].entityRefs[2]);
            ev("PLAYER3 yBoundary1 %d 2 %d new %d %d xBoundary %d %d deathBoundary %d", yBoundary1, yBoundary2, newYBoundary1, newYBoundary2,
               xBoundary1, xBoundary2, globalVariables[56]);
            ev("PLAYER2 vel %d %d speed %d tilecoll %d objint %d gravity %d cmode %d angle %d ctrl %d visible %d", playerList[0].XVelocity,
               playerList[0].YVelocity, playerList[0].speed, playerList[0].tileCollisions, playerList[0].objectInteractions,
               playerList[0].gravity, playerList[0].collisionMode, playerList[0].angle, playerList[0].controlMode, playerList[0].visible);
        }
    }
    static const char *tp = getenv("SCD32X_TELEPORT"); // "frame:x:y": the player there (pixels), standing
    if (tp && *tp && (unsigned int)atoi(tp) == frameCount) {
        const char *p = strchr(tp, ':');
        if (p && strchr(p + 1, ':')) {
            int x = atoi(p + 1), y = atoi(strchr(p + 1, ':') + 1);
            playerList[0].XPos = x << 16, playerList[0].YPos = y << 16;
            playerList[0].XVelocity = playerList[0].YVelocity = playerList[0].speed = 0;
            ev("TELEPORT %d %d", x, y);
        }
    }
    static const char *v = getenv("SCD32X_POKE");
    if (!v || !*v)
        return;
    for (const char *p = v; *p;) {
        unsigned int f = (unsigned int)strtoul(p, (char **)&p, 10);
        if (*p != ':')
            return;
        const char *n = ++p, *eq = strchr(n, '=');
        if (!eq)
            return;
        int value = (int)strtol(eq + 1, (char **)&p, 10);
        if (f == frameCount)
            for (int g = 0; g < globalVariablesCount; ++g)
                if (strlen(globalVariableNames[g]) == (size_t)(eq - n) && !strncmp(globalVariableNames[g], n, eq - n)) {
                    globalVariables[g] = value;
                    ev("POKE %.*s = %d", (int)(eq - n), n, value);
                }
        if (*p == ',')
            p++;
    }
}
// REV R4C: SCD32X_PMLOG=file - what a scene drawn by the 32X needs, frame by frame: the palette banks the lines use,
// the fade, the sprite sheets' bytes, changes to the tile pixels and to the palettes (logged when they change)
static void pm_log_frame()
{
    static FILE *f   = NULL;
    static bool once = false;
    if (!once) {
        once          = true;
        const char *p = getenv("SCD32X_PMLOG");
        if (p && *p)
            f = fopen(p, "w");
    }
    if (!f)
        return;
    static char last[256];
    static unsigned int lastTiles = 0, lastPal = 0, tileChanges = 0, palChanges = 0;
    char banks[64];
    unsigned int used = 0;
    for (int y = 0; y < SCREEN_YSIZE; ++y) used |= 1u << (gfxLineBuffer[y] & 7);
    int n = 0;
    for (int b = 0; b < 8; ++b)
        if (used >> b & 1)
            n += snprintf(banks + n, sizeof banks - n, "%d", b);
    banks[n] = 0;
    int firstSplit = -1;
    for (int y = 1; y < SCREEN_YSIZE; ++y)
        if (gfxLineBuffer[y] != gfxLineBuffer[y - 1]) {
            firstSplit = y;
            break;
        }
    static unsigned int lastLay = 0, layChanges = 0;
    unsigned int lay = 0x811C9DC5u;
    for (int l = 0; l < LAYER_COUNT; ++l) {
        TileLayer *L = &stageLayouts[l];
        for (int i = 0; i < L->ysize * 0x100 && i < TILELAYER_CHUNK_COUNT; ++i) lay = (lay ^ L->tiles[i]) * 0x01000193u;
        for (int i = 0; i < L->ysize * 128 && i < TILELAYER_SCROLL_COUNT; ++i) lay = (lay ^ L->lineScroll[i]) * 0x01000193u;
    }
    if (frameCount > 2 && lay != lastLay)
        layChanges++;
    lastLay = lay;
    unsigned int t = 0x811C9DC5u, pl = 0x811C9DC5u;
    for (int i = 0; i < TILESET_SIZE; i += 4) t = (t ^ *(const unsigned int *)(tilesetGFXData + i)) * 0x01000193u;
    for (int i = 0; i < 8 * 256; ++i) pl = (pl ^ ((const unsigned short *)fullPalette)[i]) * 0x01000193u;
    if (frameCount && t != lastTiles)
        tileChanges++;
    if (frameCount && pl != lastPal)
        palChanges++;
    lastTiles = t, lastPal = pl;
    char line[256];
    snprintf(line, sizeof line, "%d:%d banks %s split %d fade %d/%d gfx %d", activeStageList, stageListPosition, banks, firstSplit, fadeMode,
             fadeA, gfxDataPosition);
    if (strcmp(line, last)) {
        fprintf(f, "%u %s (tiles changed %u frames, palette %u frames so far)\n", frameCount, line, tileChanges, palChanges);
        strcpy(last, line);
    }
    static int maxText = 0, maxRows = 0, maxDraw = 0;
    for (int m = 0; m < TEXTMENU_COUNT; ++m) {
        if (gameMenu[m].textDataPos > maxText)
            maxText = gameMenu[m].textDataPos;
        if (gameMenu[m].rowCount > maxRows)
            maxRows = gameMenu[m].rowCount;
    }
    for (int l = 0; l < DRAWLAYER_COUNT; ++l)
        if (drawListEntries[l].listSize > maxDraw)
            maxDraw = drawListEntries[l].listSize;
    if (frameCount % 600 == 599)
        fprintf(f, "%u ... tiles changed %u frames, palette %u frames, layouts %u frames so far; menu text %d rows %d draw list %d\n",
                frameCount, tileChanges, palChanges, layChanges, maxText, maxRows, maxDraw);
    fflush(f);
}

// ---------------------------------------------------------------- REV R4C: the 32X picture mode, checked on the PC
// SCD32X_PMCHECK=file: in the scenes the console draws in the 32X picture mode (the presentation and special stage
// lists, as tools/build_game_data.py gives them their pixels) every draw call also goes to cd32x/engine/scd_pm.h - the
// console's own code - into a 256-colour picture here, two of them in turn as the console's two frame buffers; each
// frame that picture through the frame's palette (and the frame's fade, as the colours get it on the console) is
// compared with the engine's own 16-bit picture. Pixels scd_pm.h only approximates (dithered, a colour the palette
// does not have) are counted apart; any other difference is logged. The tiles as the stage loaded them are kept
// (the engine changes its own copy: Copy16x16Tile), as the console reads them from the cart.
bool scdPicture = false;
static bool pmCheckOn    = false;
static FILE *pmCheckLog  = NULL;
static unsigned char pmBuf[2][328 * 224], pmApx[2][328 * 224];
static int pmCur         = 0;
static unsigned char *pmTilesSnap = NULL;
static Vertex pmVerts[0x280];
static DrawListEntry3D pmList[0xA0];
static int pmLinesHost[224];
static int pmLayerY[4];
static bool pmLayerOn[4];
static unsigned long pmFrames = 0, pmBadFrames = 0, pmBadPixels = 0, pmApxPixels = 0, pmApxDiffer = 0;
static unsigned char pmOp[328 * 224], pmOpNow = 0; // the draw call that wrote each pixel last (a debug aid)
static unsigned long long pmOpW[32], pmOpShown[32]; // per draw call: pixels written, pixels shown (every 600 frames)
static bool pmDrew = false, pmWaitDraw = false; // a pixel written this frame; the display waits for the first such frame
#define PM_SET(off, v) (pmBuf[pmCur][(off)] = (unsigned char)(v), pmApx[pmCur][(off)] = 0, pmOp[(off)] = pmOpNow, pmOpW[pmOpNow & 31]++, pmDrew = true)
#define PM_W16(off, w) (PM_SET((off), (w) >> 8), PM_SET((off) + 1, (w) & 0xFF))
#define PM_O16(off, w)                       \
    do {                                     \
        unsigned short pmw_ = (unsigned short)(w); \
        if (pmw_ >> 8)                       \
            PM_SET((off), pmw_ >> 8);        \
        if (pmw_ & 0xFF)                     \
            PM_SET((off) + 1, pmw_ & 0xFF);  \
    } while (0)
#define PM_O8(off, v)                        \
    do {                                     \
        unsigned char pmv_ = (unsigned char)(v); \
        if (pmv_)                            \
            PM_SET((off), pmv_);             \
    } while (0)
#define PM_APPROX(off, n) memset(&pmApx[pmCur][(off)], 1, (size_t)(n))
#if SCD32X_SMALL /* the SH2 configuration (no pixels of its own): the cart image's, as the console */
#define PM_GFX ((const unsigned char *)plat_cart_data(SCD_CART_GFXDATA))
#define PM_TILES ((const unsigned char *)plat_cart_data(SCD_CART_TILEGFX))
#else
#define PM_GFX graphicData
#define PM_TILES pmTilesSnap
#endif
#define PM_CHUNK_TILE(e) (tiles128x128.gfxDataPos[(e)] >> 8)
#define PM_CHUNK_DIR(e) tiles128x128.direction[(e)]
#define PM_CHUNK_PLANE(e) tiles128x128.visualPlane[(e)]
static int pmScan[6 * 224];
#define PM_SCAN pmScan
#define PM_VERTS pmVerts
#define PM_LIST pmList
#define PM_CHECKING 1
static void pm_trace_face(const void *v, int sheetID);
#define PM_HOOK_TEXFACE(v, sheet) pm_trace_face((v), (sheet))
#define PM_SKY_FLIPY 224 /* the engine's own (224 lines: not quite the flipped row), so the PC check compares exactly */
#include "../cd32x/engine/scd_pm.h"
static void pm_check_init()
{
    static bool done = false;
    if (done)
        return;
    done          = true;
    const char *p = getenv("SCD32X_PMCHECK");
    if (p && *p) {
        pmCheckLog = fopen(p, "w");
        pmCheckOn  = pmCheckLog != NULL;
    }
    pmTilesSnap = (unsigned char *)calloc(1, TILESET_SIZE);
    for (int b = 0; b < 2; ++b)
        for (int i = 0; i < 328 * 224; ++i) pmBuf[b][i] = (unsigned char)(0x5A ^ (i * 2654435761u >> 9)); /* junk, as at power-on */
}
static int pm_trace_frame()
{
    static int f = -2;
    if (f == -2)
        f = getenv("SCD32X_PMTRACE") ? atoi(getenv("SCD32X_PMTRACE")) : -1;
    return f;
}
#define PM_TRACE(...)                                                     \
    do {                                                                  \
        if (pmCheckLog && (int)frameCount == pm_trace_frame())            \
            fprintf(pmCheckLog, "  trace: " __VA_ARGS__);                 \
    } while (0)
static void pm_trace_face(const void *v, int sheetID)
{
    const Vertex *q = (const Vertex *)v;
    PM_TRACE("texface sheet %d: (%d,%d uv %d,%d) (%d,%d uv %d,%d) (%d,%d uv %d,%d) (%d,%d uv %d,%d)\n", sheetID, q[0].x, q[0].y, q[0].u, q[0].v, q[1].x,
             q[1].y, q[1].u, q[1].v, q[2].x, q[2].y, q[2].u, q[2].v, q[3].x, q[3].y, q[3].u, q[3].v);
}
void scd_pm_clear(int index) { pmOpNow = 1; pm_clear(index); }
void scd_pm_sprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int direction, int sheetID, int alpha)
{
    pmOpNow = 2;
    PM_TRACE("sprite %d,%d %dx%d from %d,%d dir %d sheet %d alpha %d\n", XPos, YPos, width, height, sprX, sprY, direction, sheetID, alpha);
    pm_sprite(XPos, YPos, width, height, sprX, sprY, direction, sheetID, alpha);
}
void scd_pm_sprite_scaled(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX,
                          int sprY, int sheetID)
{
    pmOpNow = 3;
    PM_TRACE("scaled %d,%d\n", XPos, YPos);
    pm_sprite_scaled(direction, XPos, YPos, pivotX, pivotY, scaleX, scaleY, width, height, sprX, sprY, sheetID);
}
void scd_pm_sprite_rotozoom(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation,
                            int scale, bool zoom, int sheetID)
{
    pmOpNow = 4;
    pm_sprite_rotozoom(direction, XPos, YPos, pivotX, pivotY, sprX, sprY, width, height, rotation, scale, zoom, sheetID);
}
void scd_pm_rect(int XPos, int YPos, int width, int height, int R, int G, int B, int A) {
    pmOpNow = 5;
    PM_TRACE("rect %d,%d %dx%d rgb %d,%d,%d a %d\n", XPos, YPos, width, height, R, G, B, A);
    pm_rect(XPos, YPos, width, height, R, G, B, A);
}
void scd_pm_face(void *v, unsigned int colour) { pmOpNow = 6; pm_face(v, colour); }
void scd_pm_tex_face(void *v, int sheetID)
{
    pmOpNow = 7;
    pm_tex_face(v, sheetID);
}
void scd_pm_draw3dscene(int sheetID) { pmOpNow = 8; pm_draw3dscene(sheetID); }
static ushort pmPalAtSky[8][256];
static void pm_sky_debug(int layerID) /* the engine's Draw3DSkyLayer loop, in palette indices, against pm_sky's pixels */
{
    static int told = 0;
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    int layerWidth = layer->xsize << 7, layerHeight = layer->ysize << 7, layerYPos = layer->YPos;
    int sinValue = sinMLookupTable[layer->angle & 0x1FF], cosValue = cosMLookupTable[layer->angle & 0x1FF];
    int layerXPos = layer->XPos >> 4, layerZPos = layer->ZPos >> 4;
    static int idx[328 * 224];
    for (int i = 0; i < 328 * 224; ++i) idx[i] = -1;
    int *bufferPtr = &idx[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
    for (int i = TILE_SIZE / 2; i < SCREEN_YSIZE - TILE_SIZE; ++i) {
        int xBuffer = layerYPos / (i << 8) * -cosValue >> 9;
        int yBuffer = sinValue * (layerYPos / (i << 8)) >> 9;
        int XPos    = layerXPos + (3 * sinValue * (layerYPos / (i << 8)) >> 2) - xBuffer * SCREEN_XSIZE;
        int YPos    = layerZPos + (3 * cosValue * (layerYPos / (i << 8)) >> 2) - yBuffer * SCREEN_XSIZE;
        int lineBuffer = 0;
        while (lineBuffer < GFX_LINESIZE_DOUBLE) {
            int tileX = XPos >> 12, tileY = YPos >> 12;
            if (tileX > -1 && tileX < layerWidth && tileY > -1 && tileY < layerHeight) {
                int chunk       = tile3DFloorBuffer[(YPos >> 16 << 8) + (XPos >> 16)];
                byte *tilePixel = &tilesetGFXData[tiles128x128.gfxDataPos[chunk]];
                switch (tiles128x128.direction[chunk]) {
                    case FLIP_NONE: tilePixel += TILE_SIZE * (tileY & 0xF) + (tileX & 0xF); break;
                    case FLIP_X: tilePixel += TILE_SIZE * (tileY & 0xF) + 0xF - (tileX & 0xF); break;
                    case FLIP_Y: tilePixel += (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                    case FLIP_XY: tilePixel += 0xF - (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                    default: break;
                }
                if (*tilePixel > 0)
                    *bufferPtr = *tilePixel;
            }
            if (lineBuffer & 1)
                ++bufferPtr;
            lineBuffer++;
            XPos += xBuffer;
            YPos += yBuffer;
        }
        if (!(i & 1))
            bufferPtr -= GFX_LINESIZE;
    }
    int bad = 0;
    for (int o = 124 * 328; o < 224 * 328; ++o)
        if (idx[o] >= 0 && idx[o] != pmBuf[pmCur][o]) {
            if (told < 5 && !bad) {
                fprintf(pmCheckLog, "  sky: first at %d,%d engine index %d, picture %d\n", o % 328, o / 328, idx[o], pmBuf[pmCur][o]);
                fprintf(pmCheckLog, "  layer 0: %d x %d chunks; drawn layer %d: %d x %d; layer 0 entry (10,16) = %d\n", stageLayouts[0].xsize,
                        stageLayouts[0].ysize, activeTileLayers[layerID], layer->xsize, layer->ysize, stageLayouts[0].tiles[10 + 16 * 256]);
                int px = o % 328, rowY = o / 328, i0 = 8 + 2 * (rowY - 124);
                for (int ii = i0; ii < i0 + 2; ++ii) {
                    int xBuffer = layerYPos / (ii << 8) * -cosValue >> 9;
                    int yBuffer = sinValue * (layerYPos / (ii << 8)) >> 9;
                    int XPos    = layerXPos + (3 * sinValue * (layerYPos / (ii << 8)) >> 2) - xBuffer * SCREEN_XSIZE;
                    int YPos    = layerZPos + (3 * cosValue * (layerYPos / (ii << 8)) >> 2) - yBuffer * SCREEN_XSIZE;
                    for (int sm = 2 * px; sm < 2 * px + 2; ++sm) {
                        int X = XPos + sm * xBuffer, Y = YPos + sm * yBuffer;
                        int tileX = X >> 12, tileY = Y >> 12, v = -1, chunk = -1;
                        if (tileX > -1 && tileX < layerWidth && tileY > -1 && tileY < layerHeight) {
                            chunk           = tile3DFloorBuffer[(Y >> 16 << 8) + (X >> 16)];
                            byte *tilePixel = &tilesetGFXData[tiles128x128.gfxDataPos[chunk]];
                            switch (tiles128x128.direction[chunk]) {
                                case FLIP_NONE: tilePixel += TILE_SIZE * (tileY & 0xF) + (tileX & 0xF); break;
                                case FLIP_X: tilePixel += TILE_SIZE * (tileY & 0xF) + 0xF - (tileX & 0xF); break;
                                case FLIP_Y: tilePixel += (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                                case FLIP_XY: tilePixel += 0xF - (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                            }
                            v = *tilePixel;
                        }
                        fprintf(pmCheckLog, "    row %d sample %d: X %d Y %d chunk %d dir %d -> %d\n", ii, sm, X, Y, chunk,
                                chunk >= 0 ? tiles128x128.direction[chunk] : -1, v);
                    }
                }
            }
            bad++;
        }
    if (bad && told++ < 5)
        fprintf(pmCheckLog, "  sky: %d pixels differ from the engine's loop (frame %u)\n", bad, frameCount);
}
void scd_pm_sky(int layerID)
{
    pmOpNow = 9;
    pm_sky(layerID);
    memcpy(pmPalAtSky, fullPalette, sizeof(pmPalAtSky));
    if (getenv("SCD32X_PMSKYDBG"))
        pm_sky_debug(layerID);
}
void scd_pm_copy_tile(int dest, int src)
{
    static FILE *f = getenv("SCD32X_PMCOPYLOG") ? fopen(getenv("SCD32X_PMCOPYLOG"), "w") : NULL;
    if (f && frameCount < 40)
        fprintf(f, "%u copy %d <- %d\n", frameCount, dest, src);
    pm_copy_tile(dest, src);
    md_tile_copy_hook(dest, src);
}
const unsigned char *plat_tiles_pristine() { return pmTilesSnap; } // REV R4C: the tiles as the stage loaded them (md_check.cpp)
void pm_layer_end_host(int slot, const int *lines) { pmOpNow = (unsigned char)(10 + slot); pm_hline(slot, pmLayerY_[slot], lines); }
void scd_pm_stage_new()
{
    pm_check_init();
    pm_tiles_reset();
    if (!SCD32X_SMALL)
        memcpy(pmTilesSnap, tilesetGFXData, TILESET_SIZE);
    md_tiles_new();
}
static void pm_save_png(const char *path, const unsigned char *pic, const unsigned char *mark) /* the picture (marked pixels red) */
{
    static unsigned short img[320 * 224];
    for (int y = 0; y < 224; ++y)
        for (int x = 0; x < 320; ++x) {
            unsigned short c = fullPalette[gfxLineBuffer[y]][pic[y * 328 + x]];
            img[y * 320 + x] = mark && mark[y * 320 + x] ? 0xF800 : c;
        }
    savePNG(path, img, 320, 224, 320);
}
int pm_darkest() /* the palette index nearest black: the picture's colour where nothing is drawn (as the engine's 0) */
{
    int best = 0, bestV = 0x7FFFFFFF;
    for (int i = 0; i < 256; ++i) {
        unsigned short c = activePalette[i];
        int v            = (c >> 11) * 3 + ((c >> 5) & 63) * 2 + (c & 31);
        if (v < bestV)
            bestV = v, best = i;
    }
    return best;
}
static void pm_scene_start() /* as the console: both pictures cleared, the display off until a frame draws */
{
    int k = pm_darkest();
    memset(pmBuf, k, sizeof(pmBuf));
    memset(pmApx, 0, sizeof(pmApx));
    pmWaitDraw = true;
    pmDrew     = false;
}
static void pm_check_frame(const unsigned short *frame, int pitch)
{
    pm_frame_end();
    if (SCD32X_NODRAW) { /* the SH2 configuration: the picture is drawn (AddressSanitizer watches it), no engine picture */
        pmCur ^= 1;
        pmPalGen++;
        return;
    }
    if (!scdPicture || !pmCheckOn) {
        pmPalGen++;
        return;
    }
    if (pmWaitDraw) {
        if (!pmDrew) { /* nothing drawn yet: the console's 32X display is still off (this buffer again next frame) */
            pmPalGen++;
            return;
        }
        pmWaitDraw = false;
    }
    pmDrew = false;
    static unsigned char bad[320 * 224];
    ushort clr = 0;
    PACK_RGB888(clr, pmFadeR, pmFadeG, pmFadeB);
    int a = pmFadeA > 0xFF ? 0xFF : pmFadeA;
    const ushort *fb = &blendLookupTable[0x20 * (0xFF - a)], *pb = &blendLookupTable[0x20 * a];
    unsigned long nBad = 0, nApx = 0, nApxDiff = 0;
    int firstX = -1, firstY = -1;
    for (int y = 0; y < 224; ++y)
        for (int x = 0; x < 320; ++x) {
            ushort c = fullPalette[gfxLineBuffer[y]][pmBuf[pmCur][y * 328 + x]];
            if (a > 0) {
                int R = (fb[(c & 0xF800) >> 11] + pb[(clr & 0xF800) >> 11]) << 11;
                int G = (fb[(c & 0x7E0) >> 6] + pb[(clr & 0x7E0) >> 6]) << 6;
                int B = fb[c & 0x1F] + pb[clr & 0x1F];
                c     = (ushort)(R | G | B);
            }
            ushort e = frame[y * pitch + x];
            bad[y * 320 + x] = 0;
            if (pmApx[pmCur][y * 328 + x]) {
                nApx++;
                nApxDiff += c != e;
            }
            else if (c != e) {
                bad[y * 320 + x] = 1;
                if (!nBad++)
                    firstX = x, firstY = y;
            }
        }
    if (pm_trace_frame() == (int)frameCount) { /* (the trace frame: which draw call each pixel shows) */
        int ops[32] = { 0 };
        for (int y = 0; y < 224; ++y)
            for (int x = 0; x < 320; ++x) ops[pmOp[y * 328 + x] & 31]++;
        fprintf(pmCheckLog, "  shown pixels by draw call:");
        for (int k = 0; k < 32; ++k)
            if (ops[k])
                fprintf(pmCheckLog, " %d:%d", k, ops[k]);
        fprintf(pmCheckLog, "\n");
    }
    for (int y = 0; y < 224; ++y)
        for (int x = 0; x < 320; ++x) pmOpShown[pmOp[y * 328 + x] & 31]++;
    pmFrames++;
    pmApxPixels += nApx;
    pmApxDiffer += nApxDiff;
    if (nBad) {
        pmBadPixels += nBad;
        if (pmBadFrames++ < 40) {
            if (getenv("SCD32X_PMTILEDIFF")) {
                int n = 0;
                for (int t = 0; t < TILE_COUNT; ++t)
                    if (memcmp(&tilesetGFXData[t * 256], &pmTilesSnap[pmTileMap[t] * 256], 256) && n++ < 8)
                        fprintf(pmCheckLog, "  tile %d shows %d's pixels here, the engine's differ\n", t, pmTileMap[t]);
                fprintf(pmCheckLog, "  %d tiles differ\n", n);
            }
            if (getenv("SCD32X_PMPALDIFF")) {
                int n = 0;
                for (int b = 0; b < 8; ++b)
                    for (int i = 0; i < 256; ++i)
                        if (pmPalAtSky[b][i] != fullPalette[b][i] && n++ < 12)
                            fprintf(pmCheckLog, "  palette %d:%d %04X at the floor, %04X at the end\n", b, i, pmPalAtSky[b][i], fullPalette[b][i]);
                fprintf(pmCheckLog, "  %d palette entries changed after the floor; line bank at row %d: %d\n", n, firstY, gfxLineBuffer[firstY]);
            }
            {
                int ops[32] = { 0 };
                for (int i = 0; i < 320 * 224; ++i)
                    if (bad[i])
                        ops[pmOp[(i / 320) * 328 + i % 320] & 31]++;
                fprintf(pmCheckLog, "  by draw call (1 clear 2 sprite 3 scaled 4 rotozoom 5 rect 6 face 7 texface 8 3d 9 sky 10+ layer):");
                for (int k = 0; k < 32; ++k)
                    if (ops[k])
                        fprintf(pmCheckLog, " %d:%d", k, ops[k]);
                fprintf(pmCheckLog, "\n");
            }
            int rowLo = 224, rowHi = -1, colLo = 320, colHi = -1;
            for (int y = 0; y < 224; ++y)
                for (int x = 0; x < 320; ++x)
                    if (bad[y * 320 + x]) {
                        rowLo = y < rowLo ? y : rowLo, rowHi = y > rowHi ? y : rowHi;
                        colLo = x < colLo ? x : colLo, colHi = x > colHi ? x : colHi;
                    }
            fprintf(pmCheckLog,
                    "frame %u scene %d:%d: %lu pixels differ (first at %d,%d: picture %04X [%d] engine %04X; rows %d-%d, columns %d-%d), %lu "
                    "approximated\n",
                    frameCount, activeStageList, stageListPosition, nBad, firstX, firstY,
                    fullPalette[gfxLineBuffer[firstY]][pmBuf[pmCur][firstY * 328 + firstX]], pmBuf[pmCur][firstY * 328 + firstX],
                    frame[firstY * pitch + firstX], rowLo, rowHi, colLo, colHi, nApx);
            if (pmBadFrames <= 6 && framesDir) {
                char path[512];
                snprintf(path, sizeof(path), "%s/pm%06u.png", framesDir, frameCount);
                pm_save_png(path, pmBuf[pmCur], NULL);
                snprintf(path, sizeof(path), "%s/pm%06u_bad.png", framesDir, frameCount);
                pm_save_png(path, pmBuf[pmCur], bad);
            }
        }
    }
    if (framesDir && frameCount % saveEvery == 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s/p%06u.png", framesDir, frameCount);
        pm_save_png(path, pmBuf[pmCur], NULL);
    }
    if (frameCount % 600 == 599)
        fprintf(pmCheckLog, "%u ... %lu picture frames, %lu with differences (%lu pixels); %lu pixels approximated (%lu of them differ)\n",
                frameCount, pmFrames, pmBadFrames, pmBadPixels, pmApxPixels, pmApxDiffer);
    if (frameCount % 600 == 599) { /* which draw calls' pixels show (1 clear, 2 sprite, 3 scaled, 4 rotozoom, 5 rect, 6 face,
                                      7 textured face, 8 3D scene, 9 floor, 10+ the layer slots) */
        fprintf(pmCheckLog, "  pixels written / shown by draw call:");
        for (int k = 0; k < 32; ++k)
            if (pmOpW[k] || pmOpShown[k])
                fprintf(pmCheckLog, " %d:%llu/%llu", k, pmOpW[k], pmOpShown[k]);
        fprintf(pmCheckLog, "\n");
    }
    fflush(pmCheckLog);
    pmCur ^= 1; /* the console draws the next frame into the other buffer */
    pmPalGen++;
}
void plat_present(const unsigned short *frame, int width, int height, int pitch)
{
    pm_check_frame(frame, pitch); // REV R4C
    pm_log_frame(); // REV R4C
    draw_log_frame(); // S7e
    hostInit();
    menu_log();
    poke_frame(); // REV R4B
    md_check_frame();
    if (frameCount % saveEvery == 0) {
        if (crcLog) {
            unsigned int c = 0xFFFFFFFFu;
            for (int y = 0; y < height; y++) c = crcUpdate(c, (const byte *)(frame + y * pitch), width * 2);
            fprintf(crcLog, "%u %08x\n", frameCount, c ^ 0xFFFFFFFFu);
        }
        if (framesDir) {
            char path[512];
            snprintf(path, sizeof(path), "%s/f%06u.png", framesDir, frameCount);
            savePNG(path, frame, width, height, pitch);
        }
    }
    frameCount++;
#if SCD32X_DRAWCART
    // S7e3 (OpenJazz addendum, frame buffer rule 2): the 32X draws into the hidden buffer, which holds the
    // picture from two frames ago. SCD32X_DOUBLEBUF=1: alternate two buffers, the second filled with junk -
    // frames must stay identical (a pixel the engine does not redraw would show stale on the console, and
    // its colour conversion would run twice)
    {
        static bool on = getenv("SCD32X_DOUBLEBUF") != NULL;
        static ushort *other = NULL, *first = NULL;
        if (on && Engine.frameBuffer) {
            size_t n = (size_t)GFX_LINESIZE * SCREEN_YSIZE;
            if (!other) {
                first = Engine.frameBuffer;
                other = new ushort[n];
                for (size_t i = 0; i < n; ++i) other[i] = (ushort)(0x5A5A ^ (i * 2654435761u >> 7));
            }
            ushort *next       = (Engine.frameBuffer == first) ? other : first;
            Engine.frameBuffer = next;
        }
    }
#endif
}

// REV R4: the dev menu's DEMO PAD switch (engine/plat.hpp) - as the console: the scripted pad from each stage's start
int scdDemoPad = 0;
static unsigned int stageStartMs = 0;
static unsigned int demo_pattern(unsigned int ms)
{
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
unsigned int plat_pad()
{
    hostInit();
    unsigned int now = plat_ticks_ms(), pad = 0;
    static int demo = -1;
    if (demo < 0) {
        const char *v = getenv("SCD32X_PAD");
        demo          = v && !strcmp(v, "demo");
    }
    if (demo) { // REV R3: SCD32X_PAD=demo - the console's demo pad (cd32x/engine/plat_mars.cpp, DEMO_PAD=1), for ever
        if (now < 4000)
            return 0;
        unsigned int t = (now - 4000) % 4000;
        if (t < 150) return PAD_DOWN;
        if (t < 300) return PAD_DOWN | PAD_C;
        if (t < 900) return PAD_DOWN;
        if (t < 2400) return PAD_RIGHT;
        if (t < 2700) return PAD_RIGHT | PAD_C;
        if (t < 3900) return PAD_RIGHT;
        return 0;
    }
    for (int i = 0; i < padSteps && padScript[i].ms <= now; i++) pad = padScript[i].pad;
    static unsigned int held = 0;
    unsigned int pressed     = pad & ~held;
    held                     = pad;
    if (scdDemoPad && Engine.gameMode == ENGINE_MAINGAME) { // REV R4: the DEMO PAD switch, until a button is pressed in a stage
        if (!pressed)
            return demo_pattern(now - stageStartMs);
        scdDemoPad = 0;
    }
    if ((pad & (PAD_A | PAD_B | PAD_C | PAD_START)) == (PAD_A | PAD_B | PAD_C | PAD_START) && Engine.gameMode == ENGINE_MAINGAME &&
        plat_boot_menu())
        Engine.gameMode = ENGINE_INITDEVMENU; // REV R4: A + B + C + START - back to the dev menu, as the console
    return pad;
}

// REV R4: the dev menu. SCD32X_DEVMENU=1: the run starts in it (as the console). Its text goes into the event log
// whenever a row changes ("MENU rr |text|", highlighted characters in lower case).
bool plat_boot_menu()
{
    const char *v = getenv("SCD32X_DEVMENU");
    return v && *v && *v != '0';
}
const void *plat_native_blob() { return 0; } // REV R4: the PC builds bind the engine's part only
unsigned int plat_engine_sum() { return 0; }
void plat_credits() { ev("CREDITS (the console's credits screen: cd32x/engine/scd_credits.inc)"); } // REV R4
static char menuCur[28][41], menuShown[28][41];
static bool menuDrawn = false;
void plat_menu_clear()
{
    if (Engine.gameMode != ENGINE_DEVMENU)
        return;
    hostInit();
    memset(menuCur, ' ', sizeof(menuCur));
    for (int r = 0; r < 28; ++r) menuCur[r][40] = 0;
    menuDrawn = true;
}
void plat_menu_text(const unsigned short *t, int n, int x, int y, int highlight)
{
    if (!menuDrawn || y < 0 || (y >> 3) >= 28)
        return;
    int col = (x + 4) >> 3; // as the console: the nearest cell
    for (int i = 0; i < n; ++i, ++col) {
        int c = t[i] & 0xFF;
        if (col >= 0 && col < 40)
            menuCur[y >> 3][col] = (char)(c < 32 || c > 126 ? '?' : highlight && c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
}
static void menu_log() // the frame's menu text: the rows that changed
{
    if (!menuDrawn)
        return;
    menuDrawn = false;
    for (int r = 0; r < 28; ++r)
        if (strcmp(menuCur[r], menuShown[r])) {
            strcpy(menuShown[r], menuCur[r]);
            ev("MENU %02d |%s|", r, menuCur[r]);
        }
}

void plat_music_play(const char *fileName, int loop, unsigned int loopPoint)
{
    hostInit();
    ev("MUSIC %s loop=%d point=%u", fileName, loop, loopPoint);
}
void plat_music_stop() {}
void plat_sfx_play(int sfx, int loop)
{
    hostInit();
    ev("SFX %d %s%s", sfx, sfxList[sfx].name, loop ? " loop" : "");
}

static size_t sfxBytes = 0;
void plat_sfx_loaded(int sfx, const char *name, int rate, int channels, int bits, int samples)
{
    hostInit();
    sfxBytes += (size_t)samples * 2;
    ev("SFXLOAD %d %s %d Hz %d ch %d bit, %d samples (sfx total %zu KB as 16-bit)", sfx, name, rate, channels, bits, samples, sfxBytes / 1024);
}

// ---------------------------------------------------------------- S5c: the cart image (host)
// The normal build writes it after the first stage load (SCD32X_CARTIMG_OUT); the SH2
// configuration binds scriptCode / jumpTable / tiles128x128 / collisionMasks to it
// (SCD32X_CARTIMG_IN). Native byte order - a test image for the PC only; the console image is
// built big-endian for the SH2 later.
static size_t cart_size(int k)
{
    switch (k) {
        case SCD_CART_SCRIPTCODE: return SCRIPTDATA_COUNT * sizeof(int);
        case SCD_CART_JUMPTABLE: return JUMPTABLE_COUNT * sizeof(int);
        case SCD_CART_CHUNKS: return sizeof(Tiles128x128);
        case SCD_CART_COLLISION: return 2 * sizeof(CollisionMasks);
        case SCD_CART_SCRIPTFRAMES: return SPRITEFRAME_COUNT * sizeof(SpriteFrame);
        case SCD_CART_ANIMFRAMES: return SPRITEFRAME_COUNT * sizeof(SpriteFrame);
        case SCD_CART_ANIMLIST: return ANIMATION_COUNT * sizeof(SpriteAnimation);
        case SCD_CART_ARCTAN: return 0x100 * 0x100;
        case SCD_CART_STAGELIST: return sizeof(SceneInfo) * STAGELIST_MAX * 0x100;
        case SCD_CART_TYPENAMES: return OBJECT_COUNT * 0x40;
        case SCD_CART_GLOBALNAMES: return GLOBALVAR_COUNT * 0x20;
        case SCD_CART_ANIMFILES: return ANIFILE_COUNT * sizeof(AnimationFile);
        case SCD_CART_FONT: return FONTCHAR_COUNT * sizeof(FontCharacter);
        case SCD_CART_TRIG: return (4 * 0x200 + 2 * 0x100) * sizeof(int);
        case SCD_CART_GFXDATA: return GFXDATA_SIZE;                       // S7e: sprite sheet pixels
        case SCD_CART_TILEGFX: return TILESET_SIZE;                       // S7e: tile pixels
        case SCD_CART_BLEND: return (0x10000 + 2 * 0x2000) * sizeof(ushort); // S7e: tint, blend, subtract
    }
    return 0;
}
static void *cartBuf[SCD_CART_COUNT];
static int cartLoads = 0; // stage loads so far: image file <base>.<n>
// S5h: in the SH2 configuration the image is READ-ONLY between loads (mprotect), as the cart is for
// the SH2 - a write that bypasses the chunk patch / collision overlay stops at its line.
static size_t cart_alloc_size(int k)
{
    size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    return (cart_size(k) + pg - 1) / pg * pg;
}
static void cart_protect(bool readOnly)
{
#if SCD32X_CARTDATA
    for (int k = 0; k < SCD_CART_COUNT; ++k)
        if (cartBuf[k])
            mprotect(cartBuf[k], cart_alloc_size(k), readOnly ? PROT_READ : PROT_READ | PROT_WRITE);
#else
    (void)readOnly;
#endif
}
static bool cart_read(const char *base, int n)
{
    char path[0x200];
    snprintf(path, sizeof(path), "%s.%d", base, n);
    FILE *f       = fopen(path, "rb");
    char magic[4] = { 0 };
    bool ok       = false;
    cart_protect(false);
    if (f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "SCCI", 4)) {
        ok = true;
        for (int k = 0; k < SCD_CART_COUNT && ok; ++k) {
            uint32_t len = 0;
            ok = fread(&len, 4, 1, f) == 1 && len == cart_size(k) && fread(cartBuf[k], 1, len, f) == len;
        }
        if (!ok)
            fprintf(stderr, "cart image %s does not match this build\n", path);
    }
    if (f)
        fclose(f);
    cart_protect(true);
    return ok;
}
void *plat_cart_data(int kind)
{
    static bool init = false;
    if (!init) { // static initialisation: the engine binds its references here
        init = true;
        for (int k = 0; k < SCD_CART_COUNT; ++k) {
            if (posix_memalign(&cartBuf[k], (size_t)sysconf(_SC_PAGESIZE), cart_alloc_size(k)))
                cartBuf[k] = NULL;
            else
                memset(cartBuf[k], 0, cart_alloc_size(k));
        }
        const char *in = getenv("SCD32X_CARTIMG_IN");
        if (in && *in && !cart_read(in, 0)) // the first image now too: stage list / arctangent before any load
            fprintf(stderr, "PORT32X: no cart image %s.0\n", in);
    }
    return kind >= 0 && kind < SCD_CART_COUNT ? cartBuf[kind] : NULL;
}
// S5g: one image per stage load, as on the console (one per stage). The SH2 configuration reads
// image <n> before the n-th stage load; the normal build writes image <n> after it.
bool plat_stage_loading()
{
    { // REV R3: SCD32X_PLAYER=n - the run starts with player n (0 Sonic, 1 Tails), as the game's character choice
        static int forced = -2;
        if (forced == -2) {
            const char *p = getenv("SCD32X_PLAYER");
            forced        = p && *p ? atoi(p) : -1;
        }
        if (forced >= 0 && cartLoads == 0)
            playerListPos = forced;
    }
    { // REV R3: SCD32X_DISC_SCENES=1:0 - the scenes on the disc (the first one is the start scene). A scene that is
      // not on it restarts the start scene with the other character, as the console does (cd32x/engine/plat_mars.cpp)
        const char *v = getenv("SCD32X_DISC_SCENES");
        if (v && *v) {
            int startList = -1, startPos = -1, found = 0;
            for (const char *p = v; *p;) {
                int l = (int)strtol(p, (char **)&p, 10), s = -1;
                if (*p == ':')
                    s = (int)strtol(p + 1, (char **)&p, 10);
                if (startList < 0)
                    startList = l, startPos = s;
                if (l == activeStageList && s == stageListPosition)
                    found = 1;
                while (*p && *p != ',') p++;
                if (*p == ',')
                    p++;
            }
            if (!found && startList >= 0) {
                ev("PORT32X: scene %d:%d is not on the disc - the start scene %d:%d again, player %d -> %d", activeStageList,
                   stageListPosition, startList, startPos, playerListPos, playerListPos == 1 ? 0 : 1);
                activeStageList   = startList;
                stageListPosition = startPos;
                playerListPos     = playerListPos == 1 ? 0 : 1;
                ResetCurrentStageFolder();
            }
        }
    }
    const char *in = getenv("SCD32X_CARTIMG_IN");
    if (in && *in && !cart_read(in, cartLoads))
        ev("PORT32X: no cart image for stage load %d (kept the previous one)", cartLoads);
    pm_check_init(); // REV R4C: the 32X picture mode in the presentation and special stage lists (as the console's images)
    /* (the SH2 configuration always: its drawing is the picture mode's, as on the console) */
    scdPicture = (pmCheckOn || SCD32X_NODRAW) && (activeStageList == STAGELIST_PRESENTATION || activeStageList == STAGELIST_SPECIAL);
    if (getenv("SCD32X_NOPM")) // (a check without it)
        scdPicture = false;
    return true;
}
void plat_stage_loaded()
{
    if (scdPicture && pmCheckOn)
        pm_scene_start(); // REV R4C
    stageStartMs = plat_ticks_ms(); // REV R4: the DEMO PAD switch's pad from here
    const char *out = getenv("SCD32X_CARTIMG_OUT");
    int n           = cartLoads++;
    if (!out || !*out)
        return;
#if SCD32X_CARTDATA
    (void)n; // the SH2 configuration reads images, it does not write them
    return;
#else
    const void *src[SCD_CART_COUNT] = { scriptCode,   (const void *)plat_cart_jumptable(), &tiles128x128, collisionMasks,
                                        scriptFrames, animFrames, animationList, arcTan256LookupTable, stageList,
                                        typeNames,    globalVariableNames, animationFileList, fontCharacterList, NULL };
    static int trig[4 * 0x200 + 2 * 0x100]; // the six trig tables in one part (S5p)
    memcpy(trig, sinMLookupTable, 0x200 * sizeof(int));
    memcpy(trig + 0x200, cosMLookupTable, 0x200 * sizeof(int));
    memcpy(trig + 0x400, sin512LookupTable, 0x200 * sizeof(int));
    memcpy(trig + 0x600, cos512LookupTable, 0x200 * sizeof(int));
    memcpy(trig + 0x800, sin256LookupTable, 0x100 * sizeof(int));
    memcpy(trig + 0x900, cos256LookupTable, 0x100 * sizeof(int));
    src[SCD_CART_TRIG] = trig;
#if !SCD32X_SMALL // S7e: the renderer's data (the normal build has it all)
    static ushort blend[0x10000 + 2 * 0x2000];
    memcpy(blend, tintLookupTable, 0x10000 * sizeof(ushort));
    memcpy(blend + 0x10000, blendLookupTable, 0x2000 * sizeof(ushort));
    memcpy(blend + 0x12000, subtractLookupTable, 0x2000 * sizeof(ushort));
    src[SCD_CART_GFXDATA] = graphicData;
    src[SCD_CART_TILEGFX] = pmTilesSnap ? pmTilesSnap : tilesetGFXData; // REV R4C: the tiles as loaded (before any copy)
    src[SCD_CART_BLEND]   = blend;
#else
    static const unsigned char none[1] = { 0 };
    src[SCD_CART_GFXDATA] = src[SCD_CART_TILEGFX] = src[SCD_CART_BLEND] = none; // never written by the SH2 builds
#endif
    char path[0x200];
    snprintf(path, sizeof(path), "%s.%d", out, n);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite("SCCI", 1, 4, f);
    for (int k = 0; k < SCD_CART_COUNT; ++k) {
        uint32_t len = (uint32_t)cart_size(k);
        fwrite(&len, 4, 1, f);
        fwrite(src[k], 1, len, f);
    }
    { // S6a: which stage this image belongs to (the console image builder names its output after it)
        char key[0x90];
        snprintf(key, sizeof(key), "%s_%s", stageList[activeStageList][stageListPosition].folder, stageList[activeStageList][stageListPosition].id);
        fwrite("STAG", 1, 4, f);
        fwrite(key, 1, strlen(key) + 1, f);
    }
    { // R1U: how much script code and jump table the stage really uses - the console image keeps all of it
      // (host/cart_build trims trailing zeros, and a script's last instruction, End, is a 0)
        int32_t pos[2] = { scriptCodePos, jumpTablePos };
        fwrite("SPOS", 1, 4, f);
        fwrite(pos, sizeof(pos), 1, f);
    }
    { // REV R4C: the sheets' extent and the tile set's size - kept whole in the console image (the 32X picture mode)
        int32_t pos[2] = { gfxDataPosition, TILESET_SIZE };
        fwrite("GPOS", 1, 4, f);
        fwrite(pos, sizeof(pos), 1, f);
    }
    fclose(f);
    ev("CARTIMG written: %s (scripts %d + %d words)", path, scriptCodePos, jumpTablePos);
#endif
}

// S5c: the layout pool - 96 KB, its own allocation so AddressSanitizer sees its exact bounds
// S6a2: the chunk table as trimmed separate arrays, each its own exact-size allocation, so
// AddressSanitizer catches any read past the stage's last used chunk tile (the console image is
// trimmed the same way by host/cart_build)
void plat_chunk_arrays(const unsigned short **tileIndex, const unsigned char **direction, const unsigned char **visualPlane,
                       const unsigned char **solidity, int *n)
{
    static unsigned short *ti = NULL;
    static unsigned char *dir = NULL, *pl = NULL, *s0 = NULL, *s1 = NULL;
    const Tiles128x128 *t = (const Tiles128x128 *)plat_cart_data(SCD_CART_CHUNKS);
    int used              = 0;
    for (int i = 0; i < CHUNKTILE_COUNT; ++i)
        if (t->tileIndex[i] || t->direction[i] || t->visualPlane[i] || t->collisionFlags[0][i] || t->collisionFlags[1][i])
            used = i + 1;
    free(ti), free(dir), free(pl), free(s0), free(s1);
    int m = used ? used : 1;
    ti    = (unsigned short *)malloc(m * sizeof(unsigned short));
    dir = (unsigned char *)malloc(m), pl = (unsigned char *)malloc(m), s0 = (unsigned char *)malloc(m), s1 = (unsigned char *)malloc(m);
    for (int i = 0; i < used; ++i) {
        ti[i] = t->tileIndex[i], dir[i] = t->direction[i], pl[i] = t->visualPlane[i];
        s0[i] = t->collisionFlags[0][i], s1[i] = t->collisionFlags[1][i];
    }
    *tileIndex = ti, *direction = dir, *visualPlane = pl, solidity[0] = s0, solidity[1] = s1, *n = used;
}

// S7a: the highest position read in every data-pack file (SCD32X_FILELOG=file: written at exit as
// "<bytes> <path>" lines) - tools/build_stage_packs.py builds each stage's mini-pack from it
#include <map>
#include <string>
static std::map<std::string, int> fileUse;
static void file_log_write()
{
    const char *out = getenv("SCD32X_FILELOG");
    FILE *f         = out && *out ? fopen(out, "w") : NULL;
    if (!f)
        return;
    for (auto &e : fileUse) fprintf(f, "%d %s\n", e.second, e.first.c_str());
    fclose(f);
}
void plat_file_read(const char *path, int position)
{
    static bool on = getenv("SCD32X_FILELOG") != NULL;
    if (!on || !path || !*path)
        return;
    static bool registered = false;
    if (!registered) {
        registered = true;
        atexit(file_log_write);
    }
    auto it = fileUse.find(path);
    if (it == fileUse.end())
        fileUse[path] = position > 0 ? position : 0; // S7a3: listed even if nothing is read
    else if (position > it->second)
        it->second = position;
}

// S7e: the game's drawing, measured (SCD32X_DRAWLOG=file): per draw function calls and pixels, per
// sprite sheet calls, the sheets' sizes, the sheet bytes in use, the 3D maxima - written at exit
struct ScdDrawCount {
    long calls = 0, pixels = 0, maxPerFrame = 0, thisFrame = 0;
};
static std::map<std::string, ScdDrawCount> drawFns;
static std::map<int, long> drawSheets;
static long drawFrames = 0, drawMaxFaces = 0, drawMaxVertices = 0;
static void draw_log_write()
{
    const char *out = getenv("SCD32X_DRAWLOG");
    FILE *f         = out && *out ? fopen(out, "w") : NULL;
    if (!f)
        return;
    fprintf(f, "frames %ld\n", drawFrames);
    for (auto &e : drawFns) fprintf(f, "fn %s calls %ld pixels %ld maxperframe %ld\n", e.first.c_str(), e.second.calls, e.second.pixels, e.second.maxPerFrame);
    for (auto &e : drawSheets)
        if (e.first >= 0 && e.first < SURFACE_COUNT)
            fprintf(f, "sheet %d calls %ld file %s w %d h %d bytes %d at %d\n", e.first, e.second, gfxSurface[e.first].fileName[0] ? gfxSurface[e.first].fileName : "-",
                    gfxSurface[e.first].width, gfxSurface[e.first].height, gfxSurface[e.first].width * gfxSurface[e.first].height, gfxSurface[e.first].dataPosition);
    fprintf(f, "sheetbytes %d\n", gfxDataPosition);
    fprintf(f, "maxfaces %ld maxvertices %ld\n", drawMaxFaces, drawMaxVertices);
    fclose(f);
}
// REV R3: the engine's sprite draw calls -> the PC check's model of the Genesis hardware sprites (SCD32X_SGBTEST)
void plat_sprite(int kind, int sheet, int sx, int sy, int w, int h, int x, int y, int dir, int rot, int px, int py)
{
    md_sprite_draw(kind, sheet, sx, sy, w, h, x, y, dir, rot, px, py);
}
void plat_layout_set(int layer, int x, int y) { md_layout_set(layer, x, y); } // REV R4B
// REV R4C: SCD32X_BRAM=file - the console's backup-RAM save (cd32x/engine/scd_save.h, 512 bytes, big-endian words) in
// place of the engine's SData.bin: the same packing as the SH2's plat_save_*
#include "../cd32x/engine/scd_save.h"
int plat_save_read()
{
    const char *p = getenv("SCD32X_BRAM");
    if (!p || !*p)
        return -1;
    FILE *f = fopen(p, "rb");
    if (!f) {
        md_log("SAVE read from %s: no file", p);
        return 0;
    }
    unsigned char b[2 * SCD_SAVE_WORDS];
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n != sizeof b)
        return 0;
    uint16_t w[SCD_SAVE_WORDS];
    for (int i = 0; i < SCD_SAVE_WORDS; ++i) w[i] = (uint16_t)(b[2 * i] << 8 | b[2 * i + 1]);
    bool ok = scd_save_unpack(w, saveRAM);
    md_log("SAVE read from %s: %s", p, ok ? "ok" : "not a save");
    return ok ? 1 : 0;
}
int plat_save_write()
{
    const char *p = getenv("SCD32X_BRAM");
    if (!p || !*p)
        return -1;
    uint16_t w[SCD_SAVE_WORDS];
    int used = scd_save_pack(saveRAM, w);
    unsigned char b[2 * SCD_SAVE_WORDS];
    for (int i = 0; i < SCD_SAVE_WORDS; ++i) b[2 * i] = (unsigned char)(w[i] >> 8), b[2 * i + 1] = (unsigned char)w[i];
    FILE *f = fopen(p, "wb");
    if (!f)
        return 0;
    fwrite(b, 1, sizeof b, f);
    fclose(f);
    md_log("SAVE written to %s: %d of %d words", p, used, SCD_SAVE_WORDS);
    return 1;
}
// REV R4C: SCD32X_SAVELOG=file - every save slot a script reads or writes: frame, list:scene, R/W, slot, value
void scd_save_log(int index, int value, int write)
{
    static FILE *f   = NULL;
    static bool once = false;
    if (!once) {
        once          = true;
        const char *p = getenv("SCD32X_SAVELOG");
        if (p && *p)
            f = fopen(p, "w");
    }
    if (f) {
        fprintf(f, "%u %d:%d %c %d %d\n", frameCount, activeStageList, stageListPosition, write ? 'W' : 'R', index, value);
        fflush(f);
    }
}
void plat_draw_log(const char *function, int sheet, int pixels)
{
    static bool on = getenv("SCD32X_DRAWLOG") != NULL;
    if (!on)
        return;
    static bool registered = false;
    if (!registered) {
        registered = true;
        atexit(draw_log_write);
    }
    ScdDrawCount &c = drawFns[function];
    c.calls++;
    c.pixels += pixels > 0 ? pixels : 0;
    if (++c.thisFrame > c.maxPerFrame)
        c.maxPerFrame = c.thisFrame;
    if (sheet >= 0)
        drawSheets[sheet]++;
    if (faceCount > drawMaxFaces)
        drawMaxFaces = faceCount;
    if (vertexCount > drawMaxVertices)
        drawMaxVertices = vertexCount;
}
static void draw_log_frame() /* called once per presented frame */
{
    drawFrames++;
    for (auto &e : drawFns) e.second.thisFrame = 0;
}

// S7e: the renderer's target - only the SH2 drawing build uses it (the PC builds allocate their own)
unsigned short *plat_frame_target() { return NULL; }

// S5q: frame-buffer RAM on the PC: a separate zeroed allocation per kind (ASan sees the bounds)
unsigned char *plat_fb_data(int kind, int size)
{
    static unsigned char *mem[SCD_FB_COUNT];
    if (kind < 0 || kind >= SCD_FB_COUNT)
        return NULL;
    if (!mem[kind])
        mem[kind] = (unsigned char *)calloc(1, size);
    return mem[kind];
}

unsigned char *plat_layout_pool(int *size)
{
    static unsigned char *pool = NULL;
    const int n                = 82 * 1024; // S5q: as on the SH2 (frame buffer: record 12 | menus 34 | pool 82 KB)
    if (!pool)
        pool = (unsigned char *)calloc(1, n);
    *size = scdPicture ? PM_POOL_BYTES : n; // REV R4C: the picture mode's pool (cd32x/engine/scd_pm.h) in its scenes
    return pool;
}

void plat_log(const char *text) { ev("LOG %s", text); }
void plat_fatal(const char *text)
{
    ev("FATAL %s", text);
    exit(2);
}

bool plat_running()
{
    hostInit();
    if (frameCount >= maxFrames) {
        md_check_summary();
        ev("END %u frames, heap now %zu KB, peak %zu KB", frameCount, heapNow / 1024, heapPeak / 1024);
        return false;
    }
    return true;
}
