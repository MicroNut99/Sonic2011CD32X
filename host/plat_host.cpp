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

unsigned short *plat_layer_begin(int slot, int layer, int tileYPos, unsigned short *frame)
{
    hostInit();
    return md_layer_begin(slot, layer, tileYPos, frame);
}
void plat_layer_line(int slot, int line, int x) { md_layer_line(slot, line, x); }
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
void plat_present(const unsigned short *frame, int width, int height, int pitch)
{
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
    return true;
}
void plat_stage_loaded()
{
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
    src[SCD_CART_TILEGFX] = tilesetGFXData;
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
    *size = n;
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
