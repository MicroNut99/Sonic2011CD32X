/* PORT32X: stress test of md_planes.c alone (no engine): random layers, random camera
 * paths with jumps, wrap-around and per-line parallax; after every frame each screen
 * pixel seen through the modelled plane must show the layer's cell (name word).
 * Build: cc -std=c99 -O2 md_planes.c test_md_planes.c -o test_md_planes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md_planes.h"

static uint16_t vram[0x8000];
static void wr(void *ctx, uint16_t a, uint16_t w) { (void)ctx; vram[a >> 1] = w; }
static uint32_t rnd = 12345;
static int R(int n) { rnd = rnd * 1103515245u + 12345u; return (int)((rnd >> 8) % (uint32_t)n); }

int main(void)
{
    static uint16_t chunks[512 * 64], quads[1024 * 16], layout[256 * 16];
    int t, bad = 0, frames = 0, words = 0, maxwords = 0, skipped = 0;
    for (t = 0; t < 512 * 64; t++) chunks[t] = (uint16_t)(R(1024) | R(4) << 10 | (R(4) == 0) << 12 | (R(50) == 0) << 13);
    for (t = 0; t < 1024 * 16; t++) quads[t] = (uint16_t)((1 + R(0x7FF)) | R(4) << 13 | R(4) << 11) & 0x7FFF;
    for (int test = 0; test < 24; test++) {
        MdLayer L;
        MdPlane P;
        int wch = 1 + R(test < 6 ? 4 : 90), hch = 1 + R(test % 3 == 0 ? 2 : 16), W = wch * 128, H = hch * 128;
        int lines = test & 1 ? 240 : 224, screenW = 320;
        int bands = 1 + R(6), bandX[8], bandSpeed[8], lineBand[240];
        for (t = 0; t < wch * hch; t++) layout[t] = (uint16_t)R(512);
        L.chunks = chunks; L.quads = quads; L.layout = layout; L.layoutStride = wch;
        L.wChunks = wch; L.hChunks = hch; L.showBack = (uint8_t)(test % 4 != 1); L.showFront = (uint8_t)(test % 4 != 2); L.tileMap = 0;
        md_plane_reset(&P);
        for (t = 0; t < lines; t++) lineBand[t] = (t * bands) / lines;
        int camX = R(W), camY = R(H);
        for (t = 0; t < bands; t++) { bandX[t] = R(W); bandSpeed[t] = R(17) - 8; }
        for (int f = 0; f < 300; f++) {
            int lineX[240];
            int16_t hs[240];
            uint16_t vs;
            /* camera: steady runs, occasional jumps */
            int mode = (f / 40) % 4;
            camX += mode == 0 ? 3 : mode == 1 ? -5 : mode == 2 ? 16 : -16;
            camY += (f / 25) % 2 ? 2 : -3;
            if (R(60) == 0) { camX += R(2000) - 1000; camY += R(600) - 300; }
            camX = ((camX % W) + W) % W;
            camY = ((camY % H) + H) % H;
            for (t = 0; t < bands; t++) bandX[t] = (((bandX[t] + bandSpeed[t]) % W) + W) % W;
            for (t = 0; t < lines; t++) {
                int x = bands == 1 ? camX : (camX * (lineBand[t] + 1) / bands + bandX[lineBand[t]]);
                lineX[t] = ((x % W) + W) % W;
            }
            md_plane_update(&P, &L, 0xC000, camY, lineX, lines, screenW, hs, &vs, wr, NULL);
            words += P.written;
            if ((int)P.written > maxwords) maxwords = P.written;
            /* check every pixel's name word through the plane */
            for (int y = 0; y < lines; y++) {
                int py = (y + vs) & 255;
                if (P.overflowRows & (1u << (py >> 3))) { skipped++; continue; } /* impossible on the VDP: flagged */
                int ly = (camY + y) % H;
                for (int px = 0; px < screenW; px += 1) {
                    int plx = (px - hs[y]) & 511;
                    uint16_t got = vram[(0xC000 >> 1) + (py >> 3) * 64 + (plx >> 3)];
                    int lx = (lineX[y] + px) % W;
                    uint16_t want = md_name(&L, lx >> 3, ly >> 3);
                    if (got != want) {
                        if (bad < 5) printf("test %d frame %d line %d px %d: got %04x want %04x\n", test, f, y, px, got, want);
                        bad++;
                    }
                }
            }
            frames++;
        }
    }
    printf("%d frames, %d pixels wrong, %d lines flagged impossible, name words per frame avg %d max %d\n", frames, bad, skipped, words / frames, maxwords);
    return bad != 0;
}
