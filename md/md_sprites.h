/* REV R3: the act's sprites as Genesis hardware sprites, the way the 1993 game drew them.
 *
 * The 32X frame buffer cannot show them: the engine keeps the stage layouts and menus there (116 KB, SDRAM is
 * full), and a shown 32X picture means swapping the two frame buffers every frame. The Genesis VDP draws them
 * instead, from the same record the 68000 already copies each frame (md/sgb.h): its write groups reach any VRAM
 * address, so the sprites' patterns and the sprite attribute table travel in it - no 68000 or boot-stack change.
 *
 * The act's sprite pack ("SPRS"), made on the PC from the engine's own frames (host/md_check.cpp, SCD32X_MDGEXPORT),
 * sits in the act's MDG file after the plane data; the MDG header's last 8 bytes point at it ('SPRP', offset).
 * Every sprite frame of the act (the scripts' frames and the animation files' frames) is converted into 4bpp cells
 * on one palette line, cut into hardware sprites of up to 4 x 4 cells ("pieces"). Frames the animations rotate get
 * their 90-degree turn as a second cell set; the engine's own DrawSpriteRotated was run on the PC for every
 * direction and quarter turn, so each combination is a cell set, the VDP flips and an offset ("rotation table").
 *
 * Each engine frame the draw calls (DrawSprite, DrawSpriteFlipped, DrawSpriteRotated; the blended ones every other
 * frame, the Genesis way of see-through) are collected (mds_sprite); planes_frame then puts the pattern uploads (cell sets not yet in VRAM, a small
 * cache with least-recently-used eviction) and the sprite table into the record, before the plane writes.
 * Draw lists after the stage's last front tile layer get the priority bit: in front of the loop and tunnel tiles;
 * the others go behind them, as in the engine.
 *
 * Pack (big-endian, offsets from the pack's start):
 *   0 'SPRS'  4 u16 version (1)  6 u16 frames  8 u16 sets  10 u16 hash bits
 *  12 u32 frames (24 bytes each: u8 sheet, u8 flags (1 = rotation table), u16 sx, sy, w, h - the key -,
 *         u16 set, u8 bx, by (the opaque box in the rect), u8 cw, ch (its size in cells), u16 rotation table or 0xFFFF,
 *         s16 pivot x, y (DrawSpriteRotated's pivot arguments the rotation table was measured with), u16 0)
 *  16 u32 hash (1 << bits u16: frame + 1, 0 = empty; key hash mds_hash, linear probing)
 *  20 u32 sets (12 bytes: u32 cells offset, u16 cells, u16 first piece, u8 pieces, u8 palette line, u8 cw, ch)
 *  24 u32 pieces (4 bytes: u8 cell x, cell y, u8 VDP size bits ((w - 1) << 2 | (h - 1)), u8 first cell in the set)
 *  28 u32 rotation tables (8 x 12 bytes: [direction * 4 + quarter turn] u16 set, u8 flips, u8 0, s16 x, s16 y - the
 *         padded image's top-left from the draw position -, s8 kxx, kxy, kyx, kyy: how x, y follow a pivot one pixel
 *         off in x (kxx, kxy) and in y (kyx, kyy))
 *  40 u32 pack size
 * The sprite colours are in the MDG's own CRAM slot table (offset 16): the record's palette changes cover them.
 * Plain C, no host dependencies (the SH2 engine build and the PC model compile the same file). */
#ifndef MD_SPRITES_H
#define MD_SPRITES_H
#include <stdint.h>

#define MDS_MAGIC 0x53505253u /* 'SPRS' */
#define MDS_TAG 0x53505250u   /* 'SPRP': the MDG header's word 56 - word 60 is the pack's offset in the MDG */
#define MDS_VERSION 1
#define MDS_SAT 0xF800        /* the sprite attribute table (SH2 command 68's VDP set-up: register 5 = 0x7C) */
#define MDS_MAX_DRAWS 96      /* draw calls a frame */
#define MDS_MAX_HW 80         /* hardware sprites in H40 */
#define MDS_MAX_SETS 768
#define MDS_MAX_RES 128       /* cell sets in VRAM at once */
#define MDS_CACHE 32          /* the last draws' frames (the pack's tables are in the cart) */
#define MDS_UPLOAD_MAX 768    /* pattern words a record carries at most; a bigger set goes up over several frames and is
                                 drawn once it is all in VRAM (the 68000 copies every record word: 75 SH2 cycles each) */

#define MDS_HFLIP 1
#define MDS_VFLIP 2
#define MDS_HIGH 4

typedef struct {
    uint16_t set;
    int16_t x, y; /* the set's padded image top-left on the screen */
    uint8_t flags, pad;
} MdsDraw;

typedef struct {
    uint16_t set, base, n, used; /* VRAM cell base, cells, the frame it was last drawn */
    uint16_t firstPiece;         /* the set's own record, kept here (the pack is in the cart) */
    uint8_t np, line, cw, ch;
    uint16_t done; /* cells uploaded so far (= n: drawable) */
} MdsRes;

typedef struct {
    uint32_t k1; /* sheet << 16 | sx << 8 | sy */
    uint16_t k2; /* w << 8 | h (0: empty entry) */
    uint16_t frame, set;
    uint8_t bx, by, cw, ch, flags, pad;
} MdsCache;

typedef struct {
    const uint8_t *pack;
    int nsets, nframes, hashBits;
    uint16_t cellLo, cellHi;
    MdsDraw draw[MDS_MAX_DRAWS];
    int ndraw;
    MdsRes res[MDS_MAX_RES]; /* sorted by base */
    int nres;
    uint16_t frame;
    uint8_t resOf[MDS_MAX_SETS]; /* set -> res index + 1 (0 = not in VRAM) */
    MdsCache cache[MDS_CACHE];
    long uploadWords, dropped, hwMax, evicted;
} MdSprites;

/* the pack at p (0 = sprites off); cellLo .. cellHi - 1: the VRAM cells the sprites may use */
int mds_init(MdSprites *s, const uint8_t *p, uint16_t cellLo, uint16_t cellHi);
/* the frame for an engine draw call's sheet rect, -1 if the pack has none */
int mds_find(const MdSprites *s, int sheet, int sx, int sy, int w, int h);
/* an engine draw call: kind 0 DrawSprite / DrawSpriteFlipped (x, y = the rect's top-left, dir = the flip); kind 1
   DrawSpriteRotated (x, y = its centre, px, py = its pivot arguments, rot = its rotation); kind 2 a see-through draw
   (the blended ones: as kind 0, every other frame); high = the priority bit */
void mds_sprite(MdSprites *s, int kind, int sheet, int sx, int sy, int w, int h, int x, int y, int dir, int rot, int px, int py, int high);
/* the frame's pattern uploads and sprite table into the record (sgb_write / sgb_write_run, ctx = SgbOut); the draw
   list starts again empty */
void mds_record(MdSprites *s, void *out);
/* the VRAM holds none of the sets any more (a record that never reached the VDP) */
void mds_forget(MdSprites *s);
uint16_t mds_rd16(const uint8_t *p);
uint32_t mds_rd32(const uint8_t *p);
uint16_t mds_hash(int sheet, int sx, int sy, int w, int h);
#endif
