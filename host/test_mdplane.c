/* PORT32X: round S3b - runs md/mdplane_main.c (the 68000 demo program) on the PC against a
 * model of the VDP and checks every displayed frame against the engine's own layer image.
 *   test_mdplane MDPLANE.dat     (MDPLANE.dat.ref = the engine's CRC per frame, from the export)
 * The VDP model follows the documented behaviour the program relies on: control-port pairs
 * (VRAM / CRAM / VSRAM write addresses), register writes, auto-increment (register 15), plane
 * A / B / H-scroll bases (registers 2, 4, 13), per-line H scroll (register 11 = 3), 64x32 planes,
 * full-screen V scroll from VSRAM, the priority rule (front over behind, then A over B). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../md/sh2_frames.h"
#include "../md/mdplane_data.h"

const uint8_t *hv_blob;
volatile uint32_t vint_count;
int mdplane_main(void);

/* ---- S4 live mode, Kobo's CMD 50 pattern: the SH2 hands the frame buffer over (give_fb, FM = 0)
   and takes it back (take_fb); the 68000 only uses it in between. Modelled as on hardware:
   at boot the frame buffer holds junk (the SH2 program), and ZERO words written by the 68000
   are dropped (Kobo K14) - so the SH2 must clear it first. */
uint8_t hv_fb[0x20000];
static int fm = 1; /* 1 = the SH2 owns the frame buffer */
static uint16_t comm8, comm10;
static long violations, sh2Frames;
void hv_fb_use(void)
{
    if (fm)
        violations++; /* the 68000 uses the frame buffer the SH2 has not handed over */
}
void hv_fb_w16(uint32_t word, uint16_t v)
{
    hv_fb_use();
    if (v == 0)
        return; /* Kobo K14: a zero word from the 68000 may not land */
    hv_fb[2 * word]     = (uint8_t)(v >> 8);
    hv_fb[2 * word + 1] = (uint8_t)v;
}
void hv_comm10_write(uint16_t v) { comm10 = v; }

/* the SH2 (cd32x/sh2_planes.c, one step at a time) */
static uint8_t sdram[0x20000];
static uint16_t sdramWords[0x10000], wa[SH2F_WRITES_MAX], ww[SH2F_WRITES_MAX], rec[0x4000];
static Sh2Frames S;
static int sh2Phase = 0;
static uint16_t sh2Id = 0;
static void sh2_fb_w16(uint32_t word, uint16_t v)
{
    if (!fm)
        violations++; /* the SH2 writes the frame buffer it has handed over */
    hv_fb[2 * word]     = (uint8_t)(v >> 8);
    hv_fb[2 * word + 1] = (uint8_t)v;
}
static void sh2_step(void)
{
    uint32_t k;
    switch (sh2Phase) {
        case 0: /* clear (K14), give_fb, "yours" */
            for (k = 0; k < 0x10000; k++) sh2_fb_w16(k, 0);
            fm       = 0;
            comm8    = MDP_DATA_READY;
            sh2Phase = 1;
            break;
        case 1: /* the file is in: take_fb, copy, init */
            if (comm10 == MDP_DATA_READY) {
                uint32_t size;
                fm   = 1;
                size = (uint32_t)hv_fb[60] << 24 | hv_fb[61] << 16 | hv_fb[62] << 8 | hv_fb[63];
                memcpy(sdram, hv_fb, size);
                for (k = 0; k < (size + 1) / 2; k++) sdramWords[k] = (uint16_t)(sdram[2 * k] << 8 | sdram[2 * k + 1]);
                if (sh2f_init(&S, sdram, sdramWords, wa, ww)) {
                    fprintf(stderr, "SH2: not a format-3 data file\n");
                    exit(2);
                }
                sh2Phase = 2;
            }
            break;
        case 2: { /* build the record, write it, give_fb, number */
            int n = sh2f_next(&S, rec, 0x4000), i;
            if (n < 0) {
                fprintf(stderr, "SH2: record too big\n");
                exit(2);
            }
            for (i = 0; i < n; i++) sh2_fb_w16((uint32_t)i, rec[i]);
            fm    = 0;
            sh2Id = (uint16_t)MDP_NEXT_ID(sh2Id);
            comm8 = sh2Id;
            sh2Frames++;
            sh2Phase = 3;
            break;
        }
        case 3: /* used: take_fb */
            if (comm10 == sh2Id) {
                fm       = 1;
                sh2Phase = 2;
                sh2_step();
            }
            break;
    }
}
uint16_t hv_comm8_read(void)
{
    sh2_step();
    return comm8;
}

static uint8_t vram[0x10000];
static uint16_t cram[64], vsram[40];
static uint8_t reg[32];
static uint16_t first, addr;
static int pending, code;
static long ctrlWrites, dataWrites;

void hv_ctrl(uint16_t w)
{
    ctrlWrites++;
    if (pending) {
        addr    = (uint16_t)((first & 0x3FFF) | ((w & 3) << 14));
        code    = ((first >> 14) & 3) | ((w >> 2) & 0x3C);
        pending = 0;
    }
    else if ((w & 0xC000) == 0x8000)
        reg[(w >> 8) & 0x1F] = (uint8_t)w;
    else {
        first   = w;
        pending = 1;
    }
}

void hv_data(uint16_t w)
{
    dataWrites++;
    switch (code & 0xF) {
        case 1: vram[addr & 0xFFFE] = (uint8_t)(w >> 8), vram[(addr & 0xFFFE) + 1] = (uint8_t)w; break;
        case 3: cram[(addr >> 1) & 63] = w; break;
        case 5: vsram[(addr >> 1) % 40] = w; break;
        default: break;
    }
    addr = (uint16_t)(addr + reg[15]);
}

unsigned hv_vline(void) { return 0xE8; } /* "still in vertical blank" */

static uint16_t *words;
const uint16_t *hv_words(uint32_t offset, uint32_t count)
{
    (void)count;
    return words + offset / 2;
}

static uint32_t *refs;
static long nrefs, waits, compared, different, limit;
static int firstBad = -1;

static uint32_t crc_tab[256];
static uint32_t crc32(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    if (!crc_tab[1])
        for (uint32_t k = 0; k < 256; k++) {
            uint32_t v = k;
            for (int j = 0; j < 8; j++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_tab[k] = v;
        }
    c = ~c;
    for (i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

static int plane_pixel(uint16_t base, int hs, int vs, int x, int y)
{
    int py = (y + vs) & 255, px = (x - hs) & 511;
    int at = base + ((py >> 3) * 64 + (px >> 3)) * 2;
    uint16_t n = (uint16_t)(vram[at] << 8 | vram[at + 1]);
    int cx = px & 7, cy = py & 7;
    if (n & 0x0800)
        cx = 7 - cx;
    if (n & 0x1000)
        cy = 7 - cy;
    uint8_t b = vram[(n & 0x7FF) * 32 + cy * 4 + (cx >> 1)];
    int v     = cx & 1 ? b & 15 : b >> 4;
    if (!v)
        return 0;
    return (n >> 15) << 8 | ((n >> 13) & 3) << 4 | v;
}

static uint32_t render_crc(void)
{
    static uint8_t buf[320 * 224 * 2];
    int n = 0;
    uint16_t baseA = (uint16_t)((reg[2] & 0x38) << 10), baseB = (uint16_t)((reg[4] & 7) << 13), hsb = (uint16_t)((reg[13] & 0x3F) << 10);
    if ((reg[11] & 3) != 3 || reg[16] != 0x01 || (reg[12] & 0x81) != 0x81) {
        fprintf(stderr, "VDP set-up differs from what this model renders (reg 11 %02X, 12 %02X, 16 %02X)\n", reg[11], reg[12], reg[16]);
        exit(2);
    }
    for (int y = 8; y < 224; y++) {
        int hA = (vram[hsb + y * 4] << 8 | vram[hsb + y * 4 + 1]) & 0x3FF;
        int hB = (vram[hsb + y * 4 + 2] << 8 | vram[hsb + y * 4 + 3]) & 0x3FF;
        for (int x = 0; x < 320; x++) {
            int a = plane_pixel(baseA, hA, vsram[0] & 0x3FF, x, y), b = plane_pixel(baseB, hB, vsram[1] & 0x3FF, x, y);
            int p = (a && (a >> 8 || !(b >> 8))) ? a : b ? b : a;
            uint16_t out = 0xFFFF;
            if (p) {
                uint16_t c = cram[((p >> 4) & 3) * 16 + (p & 15)];
                out        = (uint16_t)(((c >> 1) & 7) << 6 | ((c >> 5) & 7) << 3 | ((c >> 9) & 7));
            }
            buf[n++] = (uint8_t)(out >> 8);
            buf[n++] = (uint8_t)out;
        }
    }
    return crc32(0, buf, (size_t)n);
}

void hv_vblank(void)
{
    /* the picture now on screen is the frame flushed in the previous vblank */
    if (waits++ > 0) {
        long f = (waits - 2) % nrefs;
        uint32_t c = render_crc();
        compared++;
        if (c != refs[f]) {
            different++;
            if (firstBad < 0)
                firstBad = (int)f;
        }
        if (compared >= limit) {
            printf("mdplane host test: %ld frames shown and compared (%ld recorded, played %.1f times), %ld different", compared, nrefs,
                   (double)compared / nrefs, different);
            if (firstBad >= 0)
                printf(" (first: recorded frame %d)", firstBad);
            printf("; VDP writes %ld control, %ld data", ctrlWrites, dataWrites);
            if (sh2Phase)
                printf("; SH2 live: %ld records, %ld frame-buffer protocol violations", sh2Frames, violations);
            printf("\n");
            exit(different != 0 || violations != 0);
        }
    }
    vint_count++;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_mdplane MDPLANE.dat\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *blob = malloc((size_t)n + 2);
    if (fread(blob, 1, (size_t)n, f) != (size_t)n)
        return 2;
    fclose(f);
    hv_blob = blob;
    for (long i = 0; i < (long)sizeof(hv_fb); i++) hv_fb[i] = (uint8_t)(i * 37 + 11); /* boot leftovers */
    words   = malloc((size_t)n + 2);
    for (long i = 0; i + 1 < n; i += 2) words[i / 2] = (uint16_t)(blob[i] << 8 | blob[i + 1]);

    char rp[1024];
    snprintf(rp, sizeof(rp), "%s.ref", argv[1]);
    f = fopen(rp, "rb");
    if (!f) {
        perror(rp);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    nrefs = ftell(f) / 4;
    fseek(f, 0, SEEK_SET);
    refs = malloc((size_t)nrefs * 4 + 4);
    for (long i = 0; i < nrefs; i++) {
        uint8_t b4[4];
        if (fread(b4, 1, 4, f) != 4)
            return 2;
        refs[i] = (uint32_t)b4[0] << 24 | b4[1] << 16 | b4[2] << 8 | b4[3];
    }
    fclose(f);
    if (nrefs < 1) {
        fprintf(stderr, "no recorded frames\n");
        return 2;
    }
    limit = nrefs + nrefs / 2;
    mdplane_main();
    return 3;
}
