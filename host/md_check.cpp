// PORT32X: the Mega Drive plane check (host only, round S3a).
//
// Every frame the engine draws its tile layers; the hooks in DrawHLineScrollLayer give us
// each layer on its own (the reference) and the scroll the engine used. We convert the
// loaded stage to Mega Drive data (cells, palette lines, name words), let md_planes.c (the
// code the 68000 will run) stream the two planes into a model of VRAM, render that VRAM
// the way the VDP would, and compare with the reference pixel by pixel (in 9-bit colour).
//
// Enabled with SCD32X_MDCHECK=1. Writes MDCHECK lines to the event log; with
// SCD32X_FRAMES_DIR set, saves the first frames that differ as reference|model|diff PNGs.
#include "../engine/RetroEngine.hpp"
extern "C" {
#include "../md/md_planes.h"
#include "../md/md_record.h"
#include "../md/sgb.h"
}
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

void md_log(const char *fmt, ...);
void md_save_png(const char *name, const unsigned char *rgb, int w, int h);
const char *md_frames_dir();
unsigned int md_frame_number();

namespace {

const int MAXLINES = SCREEN_YSIZE;
const uint16_t NAME_A = 0xC000, NAME_B = 0xE000, MAXCELLS = 0xC000 / 32;

bool enabled = false, inited = false;

// ---------------------------------------------------------------- frame capture
struct SlotRec {
    bool used;
    int layer, tileYPos;
    int x[MAXLINES];
};
SlotRec rec[4];
ushort *layerBuf = NULL, *refBuf = NULL, *realFrame = NULL;
ushort sentinel  = 0;
bool frameOpen   = false;

ushort pick_sentinel()
{
    static bool used[0x10000];
    memset(used, 0, sizeof(used));
    for (int b = 0; b < PALETTE_COUNT; ++b)
        for (int i = 0; i < PALETTE_SIZE; ++i) used[fullPalette[b][i]] = true;
    for (int v = 0x0821; v < 0x10000; ++v)
        if (!used[v])
            return (ushort)v;
    return 0x0821;
}

// ---------------------------------------------------------------- the converted stage
struct Stage {
    std::string key;
    bool ok = false;
    std::string error;
    std::vector<uint8_t> cells;   // 32 bytes per cell (4 bits per pixel), cell 0 blank
    std::vector<uint16_t> quads;  // [tile][flip][quadrant]
    std::vector<uint16_t> chunks; // 512 * 64
    int lines = 0;
    int slotIndex[4][16];         // engine palette index of each CRAM slot (-1 unused)
    std::vector<std::set<int>> lineSets;
    std::map<std::vector<uint8_t>, uint16_t> patterns; // canonical 8x8 pattern -> cell
    std::vector<uint8_t> tileSnap;                   // tileset at conversion
    std::vector<bool> tileUsed;
    std::vector<std::vector<uint16_t>> layoutSnap;   // per layer, at conversion
    int animUpdates = 0, animCells = 0, layoutChanges = 0;
};
Stage st;
MdPlane planeA, planeB;
int planeBLayer = -1;
std::vector<uint8_t> vram(0x10000);

// counters
long framesSeen = 0, framesCompared = 0, framesDifferent = 0, pixelsDifferent = 0, framesUnsupported = 0;
long wordsA = 0, wordsB = 0, maxWordsA = 0, maxWordsB = 0, overflowRows = 0, savedDiffs = 0;

void vram_write(void *, uint16_t addr, uint16_t word)
{
    vram[addr]     = (uint8_t)(word >> 8);
    vram[addr + 1] = (uint8_t)word;
}

// Palette lines: pack the cells' engine palette index sets into lines of 15 (as tools/scd_report.py).
bool pack_lines(std::vector<std::set<int>> sets, std::vector<std::set<int>> &out)
{
    std::sort(sets.begin(), sets.end());
    sets.erase(std::unique(sets.begin(), sets.end()), sets.end());
    std::vector<std::set<int>> keep;
    for (auto &s : sets) {
        if (s.empty())
            continue;
        if (s.size() > 15)
            return false;
        bool sub = false;
        for (auto &t : sets)
            if (&t != &s && t.size() > s.size() && std::includes(t.begin(), t.end(), s.begin(), s.end())) {
                sub = true;
                break;
            }
        if (!sub)
            keep.push_back(s);
    }
    std::vector<std::set<int>> best;
    unsigned seed = 1;
    for (int t = 0; t < 200; ++t) {
        std::vector<std::pair<double, int>> order;
        for (int i = 0; i < (int)keep.size(); ++i) {
            seed            = seed * 1103515245u + 12345u;
            double jitter   = t ? (seed >> 8) / 16777216.0 : 0;
            order.push_back({ -(double)keep[i].size() + jitter * 0.5, i });
        }
        std::sort(order.begin(), order.end());
        std::vector<std::set<int>> lines;
        for (auto &o : order) {
            const std::set<int> &s = keep[o.second];
            int pick = -1;
            long score = 0;
            for (int i = 0; i < (int)lines.size(); ++i) {
                std::set<int> u = lines[i];
                u.insert(s.begin(), s.end());
                if (u.size() > 15)
                    continue;
                int shared = (int)(lines[i].size() + s.size() - u.size());
                long sc    = t % 2 == 0 ? shared * 1000L - (long)u.size() : -(long)(u.size() - lines[i].size()) * 1000L - (long)lines[i].size();
                if (pick < 0 || sc > score) {
                    pick  = i;
                    score = sc;
                }
            }
            if (pick < 0)
                lines.push_back(s);
            else
                lines[pick].insert(s.begin(), s.end());
        }
        if (best.empty() || lines.size() < best.size())
            best = lines;
    }
    out = best;
    return best.size() <= 4;
}

// One 8x8 cell of a 16x16 tile: engine palette indices (0 = transparent).
void cell_indices(const uint8_t *tileset, int tile, int q, uint8_t *pix)
{
    const uint8_t *src = tileset + tile * 256 + (q >> 1) * 8 * 16 + (q & 1) * 8;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) pix[y * 8 + x] = src[y * 16 + x];
}

int line_for(const std::set<int> &s)
{
    for (int l = 0; l < (int)st.lineSets.size(); ++l)
        if (std::includes(st.lineSets[l].begin(), st.lineSets[l].end(), s.begin(), s.end()))
            return l;
    // a new animation frame may need a colour the line does not have yet: add it if it fits
    for (int l = 0; l < (int)st.lineSets.size(); ++l) {
        std::set<int> u = st.lineSets[l];
        u.insert(s.begin(), s.end());
        if (u.size() <= 15) {
            for (int idx : s)
                if (!st.lineSets[l].count(idx)) {
                    int slot = (int)st.lineSets[l].size() + 1;
                    st.slotIndex[l][slot] = idx;
                    st.lineSets[l].insert(idx);
                }
            return l;
        }
    }
    return -1;
}

int slot_of(int line, int idx)
{
    for (int s = 1; s < 16; ++s)
        if (st.slotIndex[line][s] == idx)
            return s;
    return 0;
}

// Convert one 16x16 tile into cells and its 4 flips x 4 quadrants of name words.
bool convert_tile(int tile)
{
    uint16_t name[4];
    for (int q = 0; q < 4; ++q) {
        uint8_t pix[64];
        cell_indices(tilesetGFXData, tile, q, pix);
        std::set<int> s;
        for (int i = 0; i < 64; ++i)
            if (pix[i])
                s.insert(pix[i]);
        if (s.empty()) {
            name[q] = 0;
            continue;
        }
        int line = line_for(s);
        if (line < 0)
            return false;
        // the pattern in CRAM slots, then its canonical form over the 4 flips
        std::vector<uint8_t> v[4];
        for (int f = 0; f < 4; ++f) {
            v[f].resize(64);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    int sx = f & 1 ? 7 - x : x, sy = f & 2 ? 7 - y : y;
                    v[f][y * 8 + x] = (uint8_t)slot_of(line, pix[sy * 8 + sx]);
                }
        }
        int fbest = 0;
        for (int f = 1; f < 4; ++f)
            if (v[f] < v[fbest])
                fbest = f;
        auto it = st.patterns.find(v[fbest]);
        uint16_t cell;
        if (it != st.patterns.end())
            cell = it->second;
        else {
            cell = (uint16_t)(st.cells.size() / 32);
            if (cell >= MAXCELLS)
                return false;
            st.patterns[v[fbest]] = cell;
            for (int i = 0; i < 64; i += 2) st.cells.push_back((uint8_t)(v[fbest][i] << 4 | v[fbest][i + 1]));
            for (int i = 0; i < 32; ++i) vram[cell * 32 + i] = st.cells[cell * 32 + i];
            if (inited && st.ok)
                st.animCells++;
        }
        // shown cell = canonical pattern flipped by fbest (flips are their own inverse)
        name[q] = (uint16_t)(line << 13 | ((fbest >> 1) & 1) << 12 | (fbest & 1) << 11 | cell);
    }
    for (int f = 0; f < 4; ++f) {
        int fh = f & 1, fv = (f >> 1) & 1;
        for (int q = 0; q < 4; ++q) {
            int qx = q & 1, qy = q >> 1;
            uint16_t n = name[(qy ^ fv) * 2 + (qx ^ fh)];
            if (n)
                n ^= (uint16_t)(fv << 12 | fh << 11);
            st.quads[(tile * 4 + f) * 4 + q] = n;
        }
    }
    return true;
}

std::string stage_key()
{
    char k[0x60];
    snprintf(k, sizeof(k), "%s/%s", stageList[activeStageList][stageListPosition].folder, stageList[activeStageList][stageListPosition].id);
    return k;
}

void convert_stage()
{
    st          = Stage();
    st.key      = stage_key();
    std::fill(vram.begin(), vram.end(), 0);
    st.cells.assign(32, 0); // cell 0: blank
    st.quads.assign(1024 * 16, 0);
    st.chunks.assign(512 * 64, 0);
    for (int l = 0; l < 4; ++l)
        for (int s = 0; s < 16; ++s) st.slotIndex[l][s] = -1;
    for (int i = 0; i < 512 * 64; ++i) {
        int vp       = tiles128x128.visualPlane[i];
        st.chunks[i] = (uint16_t)(tiles128x128.tileIndex[i] & 0x3FF | (tiles128x128.direction[i] & 3) << 10 | (vp == 1) << 12 | (vp > 1) << 13);
    }
    // tiles used by any layer's layout
    st.tileUsed.assign(1024, false);
    std::set<int> usedChunks;
    st.layoutSnap.assign(LAYER_COUNT, std::vector<uint16_t>());
    for (int l = 0; l < LAYER_COUNT; ++l) {
        TileLayer &L = stageLayouts[l];
        if (!L.xsize || !L.ysize)
            continue;
        st.layoutSnap[l].assign(L.tiles, L.tiles + TILELAYER_CHUNK_COUNT);
        for (int y = 0; y < L.ysize; ++y)
            for (int x = 0; x < L.xsize; ++x) usedChunks.insert(L.tiles[y * 0x100 + x] & 0x1FF);
    }
    for (int c : usedChunks)
        for (int e = 0; e < 64; ++e) st.tileUsed[tiles128x128.tileIndex[c * 64 + e] & 0x3FF] = true;
    // palette lines from every used cell's index set
    std::vector<std::set<int>> sets;
    for (int t = 0; t < 1024; ++t)
        if (st.tileUsed[t])
            for (int q = 0; q < 4; ++q) {
                uint8_t pix[64];
                cell_indices(tilesetGFXData, t, q, pix);
                std::set<int> s;
                for (int i = 0; i < 64; ++i)
                    if (pix[i])
                        s.insert(pix[i]);
                if (!s.empty())
                    sets.push_back(s);
            }
    if (!pack_lines(sets, st.lineSets)) {
        st.error = "tiles need more than 4 palette lines (or a cell has more than 15 colours)";
        md_log("MDCHECK stage %s: %s", st.key.c_str(), st.error.c_str());
        return;
    }
    st.lines = (int)st.lineSets.size();
    for (int l = 0; l < st.lines; ++l) {
        int s = 1;
        for (int idx : st.lineSets[l]) st.slotIndex[l][s++] = idx;
    }
    for (int t = 0; t < 1024; ++t)
        if (st.tileUsed[t] && !convert_tile(t)) {
            st.error = "tile conversion failed (out of cells or colours)";
            md_log("MDCHECK stage %s: %s", st.key.c_str(), st.error.c_str());
            return;
        }
    st.tileSnap.assign(tilesetGFXData, tilesetGFXData + TILESET_SIZE);
    st.ok = true;
    md_plane_reset(&planeA);
    md_plane_reset(&planeB);
    planeBLayer = -1;
    md_log("MDCHECK stage %s converted: %d palette lines, %d cells (%d KB), %d tiles", st.key.c_str(), st.lines,
           (int)st.cells.size() / 32, (int)st.cells.size() / 1024, (int)std::count(st.tileUsed.begin(), st.tileUsed.end(), true));
}

inline int rgb9_of565(ushort c) { return ((c >> 13) & 7) << 6 | ((c >> 8) & 7) << 3 | ((c >> 2) & 7); }

// ---------------------------------------------------------------- export for the 68000 demo (round S3b)
// SCD32X_MDEXPORT=file: from frame SCD32X_MDEXPORT_START (default 300) record the camera path of
// the two planes into the data part of MDPLANE.BIN (big-endian, see md/mdplane_data.h), until
// SCD32X_MDEXPORT_BYTES (default 114 KB) or SCD32X_MDEXPORT_FRAMES (default 3600) is reached.
// file.ref gets one CRC per frame of the engine's own layer image (lines 8..223, 9-bit colour)
// so test_mdplane can prove the 68000 program shows exactly that.
struct Export {
    bool on = false, active = false, done = false;
    std::string path;
    unsigned start = 300, maxFrames = 3600;
    size_t budget = 114 * 1024;
    std::vector<uint8_t> head, frames;
    std::vector<uint32_t> crcs;
    uint16_t cram[64];
    int format = 3;  // 3 (S4): stage tables + camera runs, the SH2 computes; 2 (S3c): records precomputed here
    MdPlane pA, pB; // the export's own planes: from reset at the recording's first frame, as the 68000 replays it
    int lineA = 0, lineB = -1, bgLayer = -1;
    unsigned nframes = 0;
    std::string stopReason;
} ex;

void put16(std::vector<uint8_t> &v, unsigned x)
{
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}
void put32(std::vector<uint8_t> &v, uint32_t x)
{
    put16(v, x >> 16);
    put16(v, x & 0xFFFF);
}
void set16(std::vector<uint8_t> &v, size_t at, unsigned x)
{
    v[at]     = (uint8_t)(x >> 8);
    v[at + 1] = (uint8_t)x;
}
void set32(std::vector<uint8_t> &v, size_t at, uint32_t x)
{
    set16(v, at, x >> 16);
    set16(v, at + 2, x & 0xFFFF);
}
uint32_t crc_table[256];
uint32_t crc32(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1;
            crc_table[i] = k;
        }
    c = ~c;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
// MD CRAM word from an engine palette index: 0000 BBB0 GGG0 RRR0
uint16_t md_color(int bank, int idx)
{
    int c9 = rgb9_of565(fullPalette[bank][idx]);
    return (uint16_t)(((c9 & 7) << 9) | (((c9 >> 3) & 7) << 5) | (((c9 >> 6) & 7) << 1));
}
void cram_now(uint16_t *c)
{
    for (int l = 0; l < 4; ++l)
        for (int s = 0; s < 16; ++s) {
            int idx      = st.slotIndex[l][s];
            c[l * 16 + s] = s && idx >= 0 ? md_color(0, idx) : 0;
        }
}
const int STATUS_CELL = 1520; // 16 hex digit cells at the top of the cell area (VRAM 0xBE00)

void export_begin(int bgLayer)
{
    // version 2 (S3c): the PC computes everything; the 68000 only copies into the VDP.
    // Static part: header, cells, CRAM. Frames: scroll + name-table writes + palette changes.
    ex.active  = true;
    ex.bgLayer = bgLayer;
    md_plane_reset(&ex.pA);
    md_plane_reset(&ex.pB);
    std::vector<uint8_t> &h = ex.head;
    h.assign(72, 0);
    h[0] = 'S', h[1] = 'C', h[2] = 'M', h[3] = 'D';
    set16(h, 4, 2);
    set16(h, 6, SCREEN_YSIZE);
    set16(h, 8, SCREEN_XSIZE);
    set32(h, 12, (uint32_t)h.size());
    set16(h, 16, (unsigned)(st.cells.size() / 32));
    h.insert(h.end(), st.cells.begin(), st.cells.end());
    cram_now(ex.cram);
    int sl = -1, ss = -1;
    for (int l = 3; l >= 0 && sl < 0; --l)
        for (int s = 15; s >= 1; --s)
            if (st.slotIndex[l][s] < 0) {
                sl = l, ss = s;
                break;
            }
    if (sl >= 0 && st.cells.size() / 32 <= STATUS_CELL) {
        set16(h, 64, (unsigned)sl);
        set16(h, 66, (unsigned)ss);
    }
    else {
        set16(h, 64, 0xFFFF);
        set16(h, 66, 0);
    }
    set32(h, 28, (uint32_t)h.size());
    for (int i = 0; i < 64; ++i) put16(h, ex.cram[i]);
    set16(h, 52, bgLayer >= 0 ? 1 : 0); // plane B used
    set32(h, 32, (uint32_t)h.size());   // frames follow
    md_log("MDEXPORT start at frame %u (format 2): %d cells, static part %d KB", md_frame_number(), (int)st.cells.size() / 32,
           (int)h.size() / 1024);
}

// format 3 (S4): S3b's format-1 layout - cells, compact tile quads and chunks, CRAM, layouts, and
// per frame each plane's first line + per-line x as runs + palette changes. The 68000 uses the
// cells and CRAM and copies the whole file to the SH2, which streams the planes itself.
void export_begin3(int bgLayer, int fgSlots, int bgSlots, std::function<bool(int, bool)> shows)
{
    ex.active  = true;
    ex.bgLayer = bgLayer;
    std::vector<uint8_t> &h = ex.head;
    h.assign(72, 0);
    h[0] = 'S', h[1] = 'C', h[2] = 'M', h[3] = 'D';
    set16(h, 4, 3);
    set16(h, 6, SCREEN_YSIZE);
    set16(h, 8, SCREEN_XSIZE);
    // compact the tiles and chunks the two planes use
    std::vector<int> layers = { 0 };
    if (bgLayer >= 0)
        layers.push_back(bgLayer);
    std::map<int, int> chunkMap, tileMap;
    for (int l : layers) {
        TileLayer &L = stageLayouts[l];
        for (int y = 0; y < L.ysize; ++y)
            for (int x = 0; x < L.xsize; ++x) {
                int c = L.tiles[y * 0x100 + x];
                if (!chunkMap.count(c)) {
                    int n       = (int)chunkMap.size();
                    chunkMap[c] = n;
                }
            }
    }
    for (auto &cm : chunkMap)
        for (int e = 0; e < 64; ++e) {
            int t = MD_CE_TILE(st.chunks[cm.first * 64 + e]);
            if (!tileMap.count(t)) {
                int n      = (int)tileMap.size();
                tileMap[t] = n;
            }
        }
    // cells
    set32(h, 12, (uint32_t)h.size());
    set16(h, 16, (unsigned)(st.cells.size() / 32));
    h.insert(h.end(), st.cells.begin(), st.cells.end());
    // quads of the compact tiles
    set32(h, 20, (uint32_t)h.size());
    std::vector<int> byIndex(tileMap.size());
    for (auto &tm : tileMap) byIndex[tm.second] = tm.first;
    for (int t : byIndex)
        for (int k = 0; k < 16; ++k) put16(h, st.quads[t * 16 + k]);
    // chunks
    set32(h, 24, (uint32_t)h.size());
    std::vector<int> chunkByIndex(chunkMap.size());
    for (auto &cm : chunkMap) chunkByIndex[cm.second] = cm.first;
    for (int c : chunkByIndex)
        for (int e = 0; e < 64; ++e) {
            uint16_t w = st.chunks[c * 64 + e];
            put16(h, (unsigned)((w & ~0x3FF) | tileMap[MD_CE_TILE(w)]));
        }
    // CRAM (with the status digit colour in a free slot)
    cram_now(ex.cram);
    int sl = -1, ss = -1;
    for (int l = 3; l >= 0 && sl < 0; --l)
        for (int s = 15; s >= 1; --s)
            if (st.slotIndex[l][s] < 0) {
                sl = l, ss = s;
                break;
            }
    if (sl >= 0 && st.cells.size() / 32 <= STATUS_CELL) {
        set16(h, 64, (unsigned)sl);
        set16(h, 66, (unsigned)ss);
    }
    else {
        set16(h, 64, 0xFFFF);
        set16(h, 66, 0);
    }
    set32(h, 28, (uint32_t)h.size());
    for (int i = 0; i < 64; ++i) put16(h, ex.cram[i]);
    // layouts
    for (int k = 0; k < 2; ++k) {
        size_t at = 36 + k * 12;
        if (k == 1 && bgLayer < 0)
            continue;
        int l        = layers[k];
        TileLayer &L = stageLayouts[l];
        int slots    = k ? bgSlots : fgSlots;
        set32(h, at, (uint32_t)h.size());
        set16(h, at + 4, L.xsize);
        set16(h, at + 6, L.ysize);
        h[at + 8] = shows(slots, false);
        h[at + 9] = shows(slots, true);
        for (int y = 0; y < L.ysize; ++y)
            for (int x = 0; x < L.xsize; ++x) put16(h, (unsigned)chunkMap[L.tiles[y * 0x100 + x]]);
    }
    set32(h, 32, (uint32_t)h.size()); // frames follow the static part
    md_log("MDEXPORT start at frame %u (format 3): %d cells, %d tiles, %d chunks, static part %d KB", md_frame_number(), (int)st.cells.size() / 32,
           (int)tileMap.size(), (int)chunkMap.size(), (int)h.size() / 1024);
}



void export_runs(std::vector<uint8_t> &v, int tileYPos, const int *x)
{
    put16(v, (unsigned)tileYPos);
    size_t at = v.size();
    put16(v, 0);
    int n = 0;
    for (int y = 0; y < SCREEN_YSIZE;) {
        int k = y;
        while (k < SCREEN_YSIZE && x[k] == x[y]) k++;
        put16(v, (unsigned)(k - y));
        put16(v, (unsigned)x[y]);
        n++;
        y = k;
    }
    set16(v, at, (unsigned)n);
}

void export_stop(const char *why)
{
    if (!ex.active || ex.done)
        return;
    ex.done       = true;
    ex.stopReason = why;
    std::vector<uint8_t> out = ex.head;
    set16(out, 10, ex.nframes);
    out.insert(out.end(), ex.frames.begin(), ex.frames.end());
    set32(out, 60, (uint32_t)out.size());
    FILE *f = fopen(ex.path.c_str(), "wb");
    if (f) {
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
    }
    std::string rp = ex.path + ".ref";
    f              = fopen(rp.c_str(), "wb");
    if (f) {
        std::vector<uint8_t> r;
        for (uint32_t c : ex.crcs) put32(r, c);
        fwrite(r.data(), 1, r.size(), f);
        fclose(f);
    }
    md_log("MDEXPORT wrote %s: %u frames (%.1f s), %d KB (%s)", ex.path.c_str(), ex.nframes, ex.nframes / 60.0, (int)out.size() / 1024, why);
}

uint32_t ref_crc()
{
    std::vector<uint8_t> b;
    b.reserve(SCREEN_XSIZE * SCREEN_YSIZE * 2);
    for (int y = 8; y < SCREEN_YSIZE; ++y)
        for (int x = 0; x < SCREEN_XSIZE; ++x) {
            ushort r = refBuf[y * GFX_LINESIZE + x];
            put16(b, r == sentinel ? 0xFFFF : (unsigned)rgb9_of565(r));
        }
    return crc32(0, b.data(), b.size());
}

// Tiles changed by scripts (animated tiles): convert again; layouts changed: rewrite the planes.
void follow_changes()
{
    int changed = 0;
    for (int t = 0; t < 1024; ++t) {
        if (!st.tileUsed[t] || !memcmp(&st.tileSnap[t * 256], &tilesetGFXData[t * 256], 256))
            continue;
        memcpy(&st.tileSnap[t * 256], &tilesetGFXData[t * 256], 256);
        if (!convert_tile(t))
            md_log("MDCHECK tile %d: animation frame does not fit the palette lines or the cells", t);
        changed++;
    }
    bool layoutChanged = false;
    for (int l = 0; l < LAYER_COUNT; ++l)
        if (!st.layoutSnap[l].empty() && memcmp(st.layoutSnap[l].data(), stageLayouts[l].tiles, TILELAYER_CHUNK_COUNT * 2)) {
            st.layoutSnap[l].assign(stageLayouts[l].tiles, stageLayouts[l].tiles + TILELAYER_CHUNK_COUNT);
            layoutChanged = true;
        }
    if (changed)
        st.animUpdates++;
    if (layoutChanged)
        st.layoutChanges++;
    if ((changed || layoutChanged) && ex.active)
        export_stop("tiles or layout changed (not in the demo format yet)");
    if (changed || layoutChanged) {
        md_plane_reset(&planeA);
        md_plane_reset(&planeB);
    }
}

MdLayer make_layer(int layer, bool back, bool front)
{
    MdLayer L;
    L.chunks       = st.chunks.data();
    L.quads        = st.quads.data();
    L.layout       = stageLayouts[layer].tiles;
    L.layoutStride = 0x100;
    L.wChunks      = stageLayouts[layer].xsize;
    L.hChunks      = stageLayouts[layer].ysize;
    L.showBack     = back;
    L.showFront    = front;
    return L;
}

// The VDP model: one plane pixel -> (CRAM line << 4 | slot), priority in bit 8; 0 = transparent.
int plane_pixel(uint16_t nameBase, int16_t hs, uint16_t vs, int x, int y)
{
    int py = (y + vs) & 255, px = (x - hs) & 511;
    uint16_t n = (uint16_t)(vram[nameBase + ((py >> 3) * 64 + (px >> 3)) * 2] << 8 | vram[nameBase + ((py >> 3) * 64 + (px >> 3)) * 2 + 1]);
    int cx = px & 7, cy = py & 7;
    if (n & 0x0800)
        cx = 7 - cx;
    if (n & 0x1000)
        cy = 7 - cy;
    int cell = n & 0x7FF;
    uint8_t b = vram[cell * 32 + cy * 4 + (cx >> 1)];
    int v     = cx & 1 ? b & 15 : b >> 4;
    if (!v)
        return 0;
    return (n >> 15) << 8 | ((n >> 13) & 3) << 4 | v;
}

// ---------------------------------------------------------------- REV R1: the act's Genesis data
// and the SH2 -> 68000 record (md/sgb.h), proved here frame by frame:
//   SCD32X_MDGEXPORT=dir  write dir/<folder>_<id>.MDG (the act's "SCMG" file) when the act is converted
//   SCD32X_SGBTEST=1      every frame: stream both planes with md_planes.c from the MDG FILE's tables (as
//                         the SH2 does), build the record (sgb_out_*), apply it to a second VRAM model
//                         with sgb_apply (what the 68000 does) and compare that picture with the engine's
std::vector<uint8_t> mdg_build()
{
    std::vector<uint8_t> h(SGB_HEADER, 0);
    h[0] = 'S', h[1] = 'C', h[2] = 'M', h[3] = 'G';
    set16(h, 4, SGB_VERSION);
    set16(h, 6, (unsigned)(st.cells.size() / 32));
    set32(h, 12, (uint32_t)h.size());
    h.insert(h.end(), st.cells.begin(), st.cells.end());
    set32(h, 16, (uint32_t)h.size());
    for (int l = 0; l < 4; ++l)
        for (int k = 0; k < 16; ++k) {
            int idx = st.slotIndex[l][k];
            put16(h, k && idx >= 0 ? (unsigned)idx : 0xFFFF);
        }
    set32(h, 20, (uint32_t)h.size());
    for (int i = 0; i < 1024 * 16; ++i) put16(h, st.quads[i]);
    set32(h, 24, (uint32_t)h.size());
    for (int i = 0; i < 512 * 64; ++i) put16(h, st.chunks[i]);
    set32(h, 28, (uint32_t)h.size());
    for (int i = 0; i < 4096; ++i) put16(h, 0);
    set32(h, 32, (uint32_t)h.size());
    uint16_t c[64];
    cram_now(c);
    for (int i = 0; i < 64; ++i) put16(h, c[i]);
    set32(h, 8, (uint32_t)h.size());
    std::string k = st.key;
    for (size_t i = 0; i < k.size() && i < 27; ++i) h[36 + i] = (uint8_t)k[i];
    return h;
}

struct Sgb {
    bool test = false, testInit = false;
    std::string exportDir, key;
    std::vector<uint16_t> quads, chunks, slots, cram0;
    std::vector<uint8_t> vram;
    MdPlane pA, pB;
    int bLayer = -1;
    uint16_t sent[64], shown[64];
    long frames = 0, different = 0, pixels = 0, overflow = 0, words = 0, maxWords = 0, waterFrames = 0, bankPixels = 0;
    long exported = 0;
} sgb;

void sgb_vram_w(void *, uint16_t addr, uint16_t word)
{
    sgb.vram[addr]     = (uint8_t)(word >> 8);
    sgb.vram[addr + 1] = (uint8_t)word;
}
uint16_t be16(const std::vector<uint8_t> &b, size_t at) { return (uint16_t)(b[at] << 8 | b[at + 1]); }
uint32_t be32(const std::vector<uint8_t> &b, size_t at) { return (uint32_t)be16(b, at) << 16 | be16(b, at + 2); }

void sgb_stage()
{
    std::vector<uint8_t> f = mdg_build();
    if (!sgb.exportDir.empty()) {
        std::string name = st.key;
        std::replace(name.begin(), name.end(), '/', '_');
        std::string path = sgb.exportDir + "/" + name + ".MDG";
        FILE *o          = fopen(path.c_str(), "wb");
        if (o) {
            fwrite(f.data(), 1, f.size(), o);
            fclose(o);
            sgb.exported++;
        }
        md_log("SGB exported %s: %d bytes, %d cells", path.c_str(), (int)f.size(), (int)be16(f, 6));
    }
    if (!sgb.test)
        return;
    // the tables FROM THE FILE (big-endian), as the SH2 and the 68000 read them
    sgb.key = st.key;
    sgb.quads.resize(1024 * 16);
    sgb.chunks.resize(512 * 64);
    sgb.slots.resize(64);
    sgb.cram0.resize(64);
    for (int i = 0; i < 1024 * 16; ++i) sgb.quads[i] = be16(f, be32(f, 20) + 2 * i);
    for (int i = 0; i < 512 * 64; ++i) sgb.chunks[i] = be16(f, be32(f, 24) + 2 * i);
    for (int i = 0; i < 64; ++i) sgb.slots[i] = be16(f, be32(f, 16) + 2 * i), sgb.cram0[i] = be16(f, be32(f, 32) + 2 * i);
    sgb.vram.assign(0x10000, 0);
    int ncells = be16(f, 6);
    for (int i = 0; i < ncells * 32; ++i) sgb.vram[i] = f[be32(f, 12) + i]; // the 68000's copy_vram of the cells
    for (int i = 0; i < 64; ++i) sgb.sent[i] = sgb.shown[i] = sgb.cram0[i];
    md_plane_reset(&sgb.pA);
    md_plane_reset(&sgb.pB);
    sgb.bLayer = -1;
}

int sgb_pixel(uint16_t nameBase, int16_t hs, uint16_t vs, int x, int y)
{
    const std::vector<uint8_t> &v = sgb.vram;
    int py = (y + vs) & 255, px = (x - hs) & 511;
    int at     = nameBase + ((py >> 3) * 64 + (px >> 3)) * 2;
    uint16_t n = (uint16_t)(v[at] << 8 | v[at + 1]);
    int cx = px & 7, cy = py & 7;
    if (n & 0x0800)
        cx = 7 - cx;
    if (n & 0x1000)
        cy = 7 - cy;
    uint8_t b = v[(n & 0x7FF) * 32 + cy * 4 + (cx >> 1)];
    int c     = cx & 1 ? b & 15 : b >> 4;
    if (!c)
        return 0;
    return (n >> 15) << 8 | ((n >> 13) & 3) << 4 | c;
}

// one frame the way the console does it: SH2 (record) -> 68000 (apply) -> VDP model
void sgb_frame(int fgSlot, int fgSlots, int bgLayer, int bgSlot, int bgSlots, std::function<bool(int, bool)> shows)
{
    if (!sgb.test || sgb.key != st.key)
        return;
    static uint16_t recw[SGB_REC_MAX];
    int16_t hsA[MAXLINES], hsB[MAXLINES];
    uint16_t vA = 0, vB = 0;
    auto layer = [&](int l, int slots) {
        MdLayer L;
        L.chunks       = sgb.chunks.data();
        L.quads        = sgb.quads.data();
        L.layout       = stageLayouts[l].tiles;
        L.layoutStride = 0x100;
        L.wChunks      = stageLayouts[l].xsize;
        L.hChunks      = stageLayouts[l].ysize;
        L.showBack     = shows(slots, false);
        L.showFront    = shows(slots, true);
        return L;
    };
    SgbOut o;
    sgb_out_begin(&o, recw, SGB_REC_MAX);
    MdLayer LA = layer(0, fgSlots);
    md_plane_update(&sgb.pA, &LA, SGB_NAME_A, rec[fgSlot].tileYPos, rec[fgSlot].x, SCREEN_YSIZE, SCREEN_XSIZE, hsA, &vA, sgb_write, &o);
    if (bgLayer >= 0) {
        if (bgLayer != sgb.bLayer) {
            md_plane_reset(&sgb.pB);
            sgb.bLayer = bgLayer;
        }
        MdLayer LB = layer(bgLayer, bgSlots);
        md_plane_update(&sgb.pB, &LB, SGB_NAME_B, rec[bgSlot].tileYPos, rec[bgSlot].x, SCREEN_YSIZE, SCREEN_XSIZE, hsB, &vB, sgb_write, &o);
    }
    else
        for (int i = 0; i < SCREEN_YSIZE; ++i) hsB[i] = 0;
    // palette: the engine's colours behind each CRAM slot, sent when they change (fades, cycles)
    uint16_t cIdx[64], cCol[64];
    int nc = 0;
    for (int i = 0; i < 64; ++i) {
        if (sgb.slots[i] == 0xFFFF)
            continue;
        uint16_t c = sgb_md_color(fullPalette[0][sgb.slots[i]]);
        if (c != sgb.sent[i]) {
            cIdx[nc]   = (uint16_t)i;
            cCol[nc++] = c;
            sgb.sent[i] = c;
        }
    }
    int n = sgb_out_end(&o, vA, vB, hsA, hsB, SCREEN_YSIZE, cIdx, cCol, nc);
    if (n < 0) { // as the SH2: too big - both planes from scratch next frame
        sgb.overflow++;
        md_plane_reset(&sgb.pA);
        md_plane_reset(&sgb.pB);
        return;
    }
    sgb.words += n;
    sgb.maxWords = std::max(sgb.maxWords, (long)n);
    // the 68000
    int16_t gA[MAXLINES], gB[MAXLINES];
    uint16_t gvA, gvB;
    int used = sgb_apply(recw, SCREEN_YSIZE, sgb_vram_w, NULL, gA, gB, &gvA, &gvB, sgb.shown);
    if (used != n)
        md_log("SGB frame %u: record %d words, the 68000 model read %d", md_frame_number(), n, used);
    // the picture, compared with the engine's (in 9-bit colour, bank 0: water banks are R4)
    long diff = 0;
    bool water = false;
    for (int y = 0; y < SCREEN_YSIZE; ++y) {
        if (gfxLineBuffer[y])
            water = true;
        for (int x = 0; x < SCREEN_XSIZE; ++x) {
            int a = sgb_pixel(SGB_NAME_A, gA[y], gvA, x, y);
            int b = bgLayer >= 0 ? sgb_pixel(SGB_NAME_B, gB[y], gvB, x, y) : 0;
            int pix = (a && (a >> 8 || !(b >> 8))) ? a : b ? b : a;
            int model = -1;
            if (pix) {
                int k = ((pix >> 4) & 3) * 16 + (pix & 15);
                if (gfxLineBuffer[y] == 0) { // bank 0: the colour the 68000 put into CRAM
                    uint16_t w = sgb.shown[k];
                    model      = ((w >> 1) & 7) << 6 | ((w >> 5) & 7) << 3 | ((w >> 9) & 7);
                }
                else { // another palette bank on this line (water, R4): the engine's colour for the same slot
                    model = sgb.slots[k] == 0xFFFF ? -2 : rgb9_of565(fullPalette[gfxLineBuffer[y]][sgb.slots[k]]);
                    if (sgb.slots[k] != 0xFFFF && rgb9_of565(fullPalette[gfxLineBuffer[y]][sgb.slots[k]]) != rgb9_of565(fullPalette[0][sgb.slots[k]]))
                        sgb.bankPixels++; // what one CRAM would show wrong here
                }
            }
            ushort r = refBuf[y * GFX_LINESIZE + x];
            int ref  = r == sentinel ? -1 : rgb9_of565(r);
            if (ref != model)
                diff++;
        }
    }
    sgb.frames++;
    if (water)
        sgb.waterFrames++;
    if (diff) {
        sgb.different++;
        sgb.pixels += diff;
        if (sgb.different <= 5)
            md_log("SGB frame %u: %ld pixels different%s", md_frame_number(), diff, water ? " (water lines)" : "");
    }
}

void compare_frame()
{
    // which layers are drawn, and in which slots (only per-line H scroll layers are supported)
    int fgSlots = 0, bgLayer = -1, bgSlots = 0;
    bool unsupported = false;
    for (int s = 0; s < 4; ++s) {
        if (activeTileLayers[s] >= LAYER_COUNT)
            continue;
        if (stageLayouts[activeTileLayers[s]].type != LAYER_HSCROLL || !rec[s].used) {
            unsupported = true;
            continue;
        }
        if (rec[s].layer == 0)
            fgSlots |= 1 << s;
        else if (bgLayer < 0 || bgLayer == rec[s].layer) {
            bgLayer = rec[s].layer;
            bgSlots |= 1 << s;
        }
        else
            unsupported = true; // two background layers at once: the VDP has two planes
    }
    if (unsupported || !fgSlots) {
        if (ex.active)
            export_stop("an unsupported layer appeared");
        framesUnsupported++;
        return;
    }
    int16_t hsA[MAXLINES], hsB[MAXLINES];
    uint16_t vsA = 0, vsB = 0;
    auto shows = [](int slots, bool front) {
        for (int s = 0; s < 4; ++s)
            if ((slots >> s & 1) && (s >= tLayerMidPoint) == front)
                return true;
        return false;
    };
    int fgSlot = __builtin_ctz(fgSlots);
    MdLayer LA = make_layer(0, shows(fgSlots, false), shows(fgSlots, true));
    md_plane_update(&planeA, &LA, NAME_A, rec[fgSlot].tileYPos, rec[fgSlot].x, SCREEN_YSIZE, SCREEN_XSIZE, hsA, &vsA, vram_write, NULL);
    wordsA += planeA.written;
    maxWordsA = std::max(maxWordsA, (long)planeA.written);
    overflowRows += planeA.overflow;
    if (bgLayer >= 0) {
        if (bgLayer != planeBLayer) {
            md_plane_reset(&planeB);
            planeBLayer = bgLayer;
        }
        int bgSlot  = __builtin_ctz(bgSlots);
        MdLayer LB  = make_layer(bgLayer, shows(bgSlots, false), shows(bgSlots, true));
        md_plane_update(&planeB, &LB, NAME_B, rec[bgSlot].tileYPos, rec[bgSlot].x, SCREEN_YSIZE, SCREEN_XSIZE, hsB, &vsB, vram_write, NULL);
        wordsB += planeB.written;
        maxWordsB = std::max(maxWordsB, (long)planeB.written);
        overflowRows += planeB.overflow;
    }
    sgb_frame(fgSlot, fgSlots, bgLayer, bgLayer >= 0 ? __builtin_ctz(bgSlots) : 0, bgSlots, shows); // REV R1
    // render the model and compare (backdrop = nothing drawn)
    long diff = 0;
    static std::vector<unsigned char> img;
    bool save = md_frames_dir() && savedDiffs < 5;
    if (save)
        img.assign(SCREEN_XSIZE * 3 * SCREEN_YSIZE * 3, 0);
    for (int y = 0; y < SCREEN_YSIZE; ++y) {
        int bank = gfxLineBuffer[y];
        for (int x = 0; x < SCREEN_XSIZE; ++x) {
            int a = plane_pixel(NAME_A, hsA[y], vsA, x, y);
            int b = bgLayer >= 0 ? plane_pixel(NAME_B, hsB[y], vsB, x, y) : 0;
            int pix = 0;
            if (a && (a >> 8 || !(b >> 8)))
                pix = a;
            else if (b)
                pix = b;
            else
                pix = a;
            int model = -1;
            if (pix) {
                int idx = st.slotIndex[(pix >> 4) & 3][pix & 15];
                model   = idx < 0 ? -2 : rgb9_of565(fullPalette[bank][idx]);
            }
            ushort r = refBuf[y * GFX_LINESIZE + x];
            int ref  = r == sentinel ? -1 : rgb9_of565(r);
            if (ref != model)
                diff++;
            if (save) {
                auto put = [&](int panel, int c9) {
                    unsigned char *p = &img[(y * SCREEN_XSIZE * 3 + panel * SCREEN_XSIZE + x) * 3];
                    if (c9 < 0) {
                        p[0] = 255, p[1] = 0, p[2] = 255; // magenta: nothing drawn
                        return;
                    }
                    p[0] = (unsigned char)(((c9 >> 6) & 7) * 255 / 7);
                    p[1] = (unsigned char)(((c9 >> 3) & 7) * 255 / 7);
                    p[2] = (unsigned char)((c9 & 7) * 255 / 7);
                };
                put(0, ref);
                put(1, model);
                unsigned char *p = &img[(y * SCREEN_XSIZE * 3 + 2 * SCREEN_XSIZE + x) * 3];
                p[0] = p[1] = p[2] = ref != model ? 255 : 0;
            }
        }
    }
    framesCompared++;
    if (ex.on && !ex.done) {
        if (!ex.active && md_frame_number() >= ex.start) {
            if (ex.format == 3)
                export_begin3(bgLayer, fgSlots, bgSlots, shows);
            else
                export_begin(bgLayer);
        }
        if (ex.active) {
            std::vector<uint8_t> fr;
            uint16_t now[64], cIdx[64], cCol[64];
            int nc = 0;
            cram_now(now);
            for (int i = 0; i < 64; ++i)
                if (now[i] != ex.cram[i]) {
                    cIdx[nc]   = (uint16_t)i;
                    cCol[nc++] = now[i];
                    ex.cram[i] = now[i];
                }
            if (ex.format == 3) {
                export_runs(fr, rec[fgSlot].tileYPos, rec[fgSlot].x);
                if (bgLayer >= 0)
                    export_runs(fr, rec[__builtin_ctz(bgSlots)].tileYPos, rec[__builtin_ctz(bgSlots)].x);
                else
                    put16(fr, 0), put16(fr, 0);
                put16(fr, (unsigned)nc);
                for (int i = 0; i < nc; ++i) put16(fr, cIdx[i]), put16(fr, cCol[i]);
            }
            else {
                // format 2: the record, built by the same code the SH2 uses (md/md_record.c)
                static uint16_t wa[8192], ww[8192], out[32768];
                int16_t ehA[MAXLINES], ehB[MAXLINES];
                MdWrites w   = { wa, ww, 0, 8192 };
                MdLayer LA2  = make_layer(0, shows(fgSlots, false), shows(fgSlots, true));
                MdLayer LB2;
                int bgSlot2  = bgLayer >= 0 ? __builtin_ctz(bgSlots) : 0;
                if (bgLayer >= 0)
                    LB2 = make_layer(bgLayer, shows(bgSlots, false), shows(bgSlots, true));
                int n = md_record_frame(&ex.pA, &LA2, rec[fgSlot].tileYPos, rec[fgSlot].x, bgLayer >= 0 ? &ex.pB : 0, bgLayer >= 0 ? &LB2 : 0,
                                        bgLayer >= 0 ? rec[bgSlot2].tileYPos : 0, bgLayer >= 0 ? rec[bgSlot2].x : 0, SCREEN_YSIZE, SCREEN_XSIZE,
                                        cIdx, cCol, nc, &w, ehA, ehB, out, 32768);
                for (int i = 0; i < n; ++i) put16(fr, out[i]);
            }
            if (bgLayer != ex.bgLayer)
                export_stop("the background layer changed");
            else if (ex.head.size() + ex.frames.size() + fr.size() > ex.budget)
                export_stop("Word RAM budget reached");
            else {
                ex.frames.insert(ex.frames.end(), fr.begin(), fr.end());
                ex.crcs.push_back(ref_crc());
                if (++ex.nframes >= ex.maxFrames)
                    export_stop("frame limit reached");
            }
        }
    }
    if (diff) {
        framesDifferent++;
        pixelsDifferent += diff;
        if (framesDifferent <= 10)
            md_log("MDCHECK frame %u: %ld pixels different", md_frame_number(), diff);
        if (save) {
            char name[64];
            snprintf(name, sizeof(name), "mdcheck_%06u.png", md_frame_number());
            md_save_png(name, img.data(), SCREEN_XSIZE * 3, SCREEN_YSIZE);
            savedDiffs++;
        }
    }
}

} // namespace

// ---------------------------------------------------------------- hooks (plat_host.cpp calls these)
void md_check_init()
{
    const char *v = getenv("SCD32X_MDCHECK");
    enabled       = v && *v == '1';
    if (enabled && (v = getenv("SCD32X_MDEXPORT")) && *v) {
        ex.on   = true;
        ex.path = v;
        if ((v = getenv("SCD32X_MDEXPORT_START")))
            ex.start = (unsigned)atoi(v);
        if ((v = getenv("SCD32X_MDEXPORT_FRAMES")))
            ex.maxFrames = (unsigned)atoi(v);
        if ((v = getenv("SCD32X_MDEXPORT_BYTES")))
            ex.budget = (size_t)atoi(v);
        if ((v = getenv("SCD32X_MDEXPORT_FORMAT")))
            ex.format = atoi(v) == 2 ? 2 : 3;
    }
    if (enabled && (v = getenv("SCD32X_MDGEXPORT")) && *v) // REV R1
        sgb.exportDir = v;
    if (enabled && (v = getenv("SCD32X_SGBTEST")) && *v == '1')
        sgb.test = true;
    if (enabled) {
        layerBuf = new ushort[GFX_LINESIZE * SCREEN_YSIZE];
        refBuf   = new ushort[GFX_LINESIZE * SCREEN_YSIZE];
    }
    inited = true;
}

ushort *md_layer_begin(int slot, int layer, int tileYPos, ushort *frame)
{
    if (!enabled)
        return frame;
    if (!frameOpen) {
        frameOpen = true;
        sentinel  = pick_sentinel();
        for (int i = 0; i < GFX_LINESIZE * SCREEN_YSIZE; ++i) refBuf[i] = sentinel;
        for (int s = 0; s < 4; ++s) rec[s].used = false;
    }
    rec[slot].used     = true;
    rec[slot].layer    = layer;
    rec[slot].tileYPos = tileYPos;
    realFrame          = frame;
    for (int i = 0; i < GFX_LINESIZE * SCREEN_YSIZE; ++i) layerBuf[i] = sentinel;
    return layerBuf;
}

void md_layer_line(int slot, int line, int x)
{
    if (enabled && line >= 0 && line < MAXLINES)
        rec[slot].x[line] = x;
}

void md_layer_end(int slot)
{
    if (!enabled)
        return;
    for (int i = 0; i < GFX_LINESIZE * SCREEN_YSIZE; ++i)
        if (layerBuf[i] != sentinel) {
            realFrame[i] = layerBuf[i];
            refBuf[i]    = layerBuf[i];
        }
}

// Test only (SCD32X_TEST_CAMERA=1): with no player in the stage the engine leaves the camera
// alone, so the check can drive it along a path of runs, reversals and jumps.
void test_camera()
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("SCD32X_TEST_CAMERA");
        on            = v && *v == '1';
    }
    if (!on || cameraTarget >= 0)
        return;
    static unsigned int rs = 99;
    static int f           = 0;
    f++;
    int maxX = stageLayouts[0].xsize * 128 - SCREEN_XSIZE, maxY = stageLayouts[0].ysize * 128 - SCREEN_YSIZE;
    int mode = (f / 50) % 5;
    xScrollOffset += mode == 0 ? 4 : mode == 1 ? -7 : mode == 2 ? 16 : mode == 3 ? -1 : 9;
    yScrollOffset += (f / 30) % 2 ? 3 : -2;
    rs = rs * 1103515245u + 12345u;
    if ((rs >> 8) % 90 == 0) {
        xScrollOffset += (int)((rs >> 12) % 1600) - 800;
        yScrollOffset += (int)((rs >> 4) % 400) - 200;
    }
    xScrollOffset = std::max(0, std::min(maxX, xScrollOffset));
    yScrollOffset = std::max(0, std::min(maxY, yScrollOffset));
    // palette animation as scripts do it (waterfalls...): cycle 4 colours the stage uses
    if (f % 4 == 0)
        for (int k = 0; k < 4; ++k) {
            int v = ((f / 4 + k) * 37) & 255;
            SetPaletteEntry(0, (byte)(130 + k), (byte)v, (byte)(255 - v), (byte)(v ^ 0x5A));
        }
}

// S5b: the camera log (SCD32X_CAMLOG=file) - one line per frame: camera, active layers, each
// layer's first line + a CRC of its per-line x, a CRC of all palettes and of ALL object state.
// The normal build and the SH2 configuration (no-draw, small arrays) must write identical logs.
FILE *camlog = NULL;
void camlog_frame()
{
    static int opened = 0;
    if (!opened) {
        const char *v = getenv("SCD32X_CAMLOG");
        opened        = 1;
        if (v && *v)
            camlog = fopen(v, "w");
    }
    if (!camlog)
        return;
    fprintf(camlog, "%u x%d y%d L%d,%d,%d,%d", md_frame_number(), xScrollOffset, yScrollOffset, activeTileLayers[0], activeTileLayers[1],
            activeTileLayers[2], activeTileLayers[3]);
    if (frameOpen)
        for (int sl = 0; sl < 4; ++sl)
            if (rec[sl].used)
                fprintf(camlog, " s%d:%d/%d/%08x", sl, rec[sl].layer, rec[sl].tileYPos,
                        crc32(0, (const uint8_t *)rec[sl].x, sizeof(int) * SCREEN_YSIZE));
    // S5c: the foreground layout's contents (its real rows) - the layout pool must hold the same
    uint32_t fg = 0;
    if (stageLayouts[0].ysize)
        fg = crc32(0, (const uint8_t *)stageLayouts[0].tiles, (size_t)stageLayouts[0].ysize * 0x100 * sizeof(ushort));
    // S5k: the RAM state the loaders build (hitboxes, sprite-sheet table) - a skipped loader shows here
    uint32_t h = crc32(crc32(0, (const uint8_t *)hitboxList, sizeof(hitboxList)), (const uint8_t *)gfxSurface, sizeof(gfxSurface));
    fprintf(camlog, " P%08x E%08x F%08x H%08x\n", crc32(0, (const uint8_t *)fullPalette, sizeof(fullPalette)),
            crc32(0, (const uint8_t *)objectEntityList, sizeof(objectEntityList)), fg, h);
}

// S5e: the real maxima of what the engine uses (printed with the summary: "MAXIMA ...")
struct Maxima {
    int objSlot = -1, objCount = 0, drawList[DRAWLAYER_COUNT] = { 0 }, vertices = 0, faces = 0, menuRows = 0;
    int sfx = 0, scriptFrames = 0, animFrames = 0, animations = 0, code = 0, jumps = 0, fgRows = 0, bgRows = 0;
    int menuText = 0, saveTop = -1; // S5j: highest textDataPos of a menu, highest save slot in use
    int poolBytes = 0;              // S5l: what the layout pool would hold (every layer's rows)
} mx;
void maxima_frame()
{
    int n = 0;
    for (int i = 0; i < ENTITY_COUNT; ++i)
        if (objectEntityList[i].type) {
            n++;
            if (i < TEMPENTITY_START && i > mx.objSlot)
                mx.objSlot = i;
        }
    mx.objCount = std::max(mx.objCount, n);
    for (int l = 0; l < DRAWLAYER_COUNT; ++l) mx.drawList[l] = std::max(mx.drawList[l], drawListEntries[l].listSize);
    mx.vertices = std::max(mx.vertices, vertexCount);
    mx.faces    = std::max(mx.faces, faceCount);
    for (int m = 0; m < TEXTMENU_COUNT; ++m) mx.menuRows = std::max(mx.menuRows, (int)gameMenu[m].rowCount);
    mx.sfx          = std::max(mx.sfx, globalSFXCount + stageSFXCount);
    mx.scriptFrames = std::max(mx.scriptFrames, scriptFrameCount);
    mx.animFrames   = std::max(mx.animFrames, animFrameCount);
    mx.animations   = std::max(mx.animations, animationCount);
    mx.code         = std::max(mx.code, scriptCodePos);
    mx.jumps        = std::max(mx.jumps, jumpTablePos);
    mx.fgRows       = std::max(mx.fgRows, (int)stageLayouts[0].ysize);
    for (int l = 1; l < LAYER_COUNT; ++l) mx.bgRows = std::max(mx.bgRows, (int)stageLayouts[l].ysize);
    for (int m = 0; m < TEXTMENU_COUNT; ++m) mx.menuText = std::max(mx.menuText, gameMenu[m].textDataPos);
    {
        int pool = 0;
        for (int l = 0; l < LAYER_COUNT; ++l)
            if (stageLayouts[l].ysize)
                pool += (stageLayouts[l].ysize * (0x100 * 2 + CHUNK_SIZE) + 3) & ~3;
        mx.poolBytes = std::max(mx.poolBytes, pool);
    }
    for (int i = SAVEDATA_SIZE - 1; i > mx.saveTop; --i)
        if (saveRAM[i]) {
            mx.saveTop = i;
            break;
        }
}

#if SCD32X_CARTDATA
// S5h: self-test of the chunk patch / collision overlay (SCD32X_TEST_OVERLAY=1, SH2 configuration)
static void overlay_selftest()
{
    std::string bad;
    int c = 100, d = 101;
    int i0 = tiles128x128.tileIndex[c], dir0 = tiles128x128.direction[c], s1 = tiles128x128.collisionFlags[1][c];
    int nb  = tiles128x128.tileIndex[d];
    tiles128x128.tileIndex[c] = (ushort)(i0 + 7);
    if ((int)tiles128x128.tileIndex[c] != ((i0 + 7) & 0xFFFF))
        bad += " patched-read";
    if ((int)tiles128x128.direction[c] != dir0)
        bad += " other-field";
    if ((int)tiles128x128.tileIndex[d] != nb || scd_chunk_entry(d, false))
        bad += " neighbour";
    tiles128x128.collisionFlags[1][c] = (byte)(s1 ^ 3);
    if ((int)tiles128x128.collisionFlags[1][c] != (s1 ^ 3) || (int)tiles128x128.tileIndex[c] != ((i0 + 7) & 0xFFFF))
        bad += " second-write";
    if (scd_chunk_base(SCD_CF_INDEX, c) != i0)
        bad += " image-changed";
    scd_chunk_patches_reset();
    if ((int)tiles128x128.tileIndex[c] != i0 || (int)tiles128x128.collisionFlags[1][c] != s1 || scd_chunk_entry(c, false))
        bad += " reset";
    uint a0                         = collisionMasks[1].angles[5];
    collisionMasks[1].angles[5] = 0x12345678;
    if (collisionMasks[1].angles[5] != 0x12345678)
        bad += " collision-copy";
    collisionMasks[1].angles[5] = a0;
    md_log("overlay self-test: %s", bad.empty() ? "OK" : ("FAILED:" + bad).c_str());
}
#endif

void md_check_frame()
{
    if (!enabled)
        return;
#if SCD32X_CARTDATA
    {
        static bool done = false;
        const char *v    = getenv("SCD32X_TEST_OVERLAY");
        if (!done && v && *v == '1' && frameOpen) {
            done = true;
            overlay_selftest();
        }
    }
#endif
    camlog_frame();
    maxima_frame();
    if (!frameOpen)
        return;
#if SCD32X_SMALL
    frameOpen = false; // the SH2 configuration has no tileset pixels: no conversion / comparison
    test_camera();     // (test only: the same camera path and palette cycling as the normal build)
    return;
#endif
    { // S5m: light mode (SCD32X_MDCHECK_LIGHT=1) - camera log and maxima only, no plane check
        static int light = -1;
        if (light < 0) {
            const char *v = getenv("SCD32X_MDCHECK_LIGHT");
            light         = v && *v == '1';
        }
        if (light) {
            frameOpen = false;
            test_camera();
            return;
        }
    }
    frameOpen = false;
    framesSeen++;
    if (stage_key() != st.key) {
        if (ex.active)
            export_stop("the stage changed");
        convert_stage();
        if (st.ok && (sgb.test || !sgb.exportDir.empty()))
            sgb_stage(); // REV R1
    }
    if (!st.ok)
        return;
    follow_changes();
    compare_frame();
    test_camera(); // after the comparison: it changes the camera and palette for the NEXT frame
}

void md_check_summary()
{
    if (!enabled)
        return;
    md_log("MAXIMA %s objslot %d objects %d drawlists %d,%d,%d,%d,%d,%d,%d,%d vertices %d faces %d menurows %d sfx %d "
           "scriptframes %d animframes %d animations %d code %d jumps %d fgrows %d bgrows %d menutext %d savetop %d poolkb %d saveidx %d saveblocks %08x",
           stage_key().c_str(), mx.objSlot, mx.objCount, mx.drawList[0], mx.drawList[1], mx.drawList[2], mx.drawList[3], mx.drawList[4],
           mx.drawList[5], mx.drawList[6], mx.drawList[7], mx.vertices, mx.faces, mx.menuRows, mx.sfx, mx.scriptFrames, mx.animFrames,
           mx.animations, mx.code, mx.jumps, mx.fgRows, mx.bgRows, mx.menuText, mx.saveTop, (mx.poolBytes + 1023) / 1024, scdSaveIndexMax, scdSaveBlocks);
    export_stop("the run ended");
    md_log("MDCHECK summary: %ld frames with layers, %ld compared, %ld with differences (%ld pixels), %ld unsupported "
           "(V-scroll/3D layers or two backgrounds)",
           framesSeen, framesCompared, framesDifferent, pixelsDifferent, framesUnsupported);
    if (framesCompared)
        md_log("MDCHECK streaming: name words per frame plane A avg %ld max %ld, plane B avg %ld max %ld; rows the VDP cannot show: %ld",
               wordsA / framesCompared, maxWordsA, wordsB / framesCompared, maxWordsB, overflowRows);
    md_log("MDCHECK stage %s: %d palette lines, %d cells, animated-tile updates %d (%d new cells), layout changes %d", st.key.c_str(), st.lines,
           (int)st.cells.size() / 32, st.animUpdates, st.animCells, st.layoutChanges);
    if (sgb.test) // REV R1
        md_log("SGB summary: %ld frames through record + 68000 model, %ld with differences (%ld pixels), %ld with water lines, "
               "%ld records too big; record words avg %ld max %ld (limit %d); pixels whose colour needs a second palette bank: %ld",
               sgb.frames, sgb.different, sgb.pixels, sgb.waterFrames, sgb.overflow, sgb.frames ? sgb.words / sgb.frames : 0, sgb.maxWords,
               SGB_REC_MAX, sgb.bankPixels);
}
