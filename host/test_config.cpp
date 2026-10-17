// PORT32X: checks tools/scd_report.py's config and bytecode parsers against the engine.
// usage: test_config Data.rsdk  -> prints what the engine read, in the same text form
// that tools/test_scd_report.sh expects from the Python side.
#include "../engine/RetroEngine.hpp"

void plat_sfx_loaded(int sfx, const char *name, int rate, int channels, int bits, int samples);
static int loadedCount = 0;
static char loaded[64][0x80];
static int loadedSamples[64];
// The host platform's hook is replaced here (this test links without plat_host.o).
void plat_sfx_loaded(int sfx, const char *name, int rate, int channels, int bits, int samples)
{
    if (loadedCount < 64) {
        snprintf(loaded[loadedCount], 0x80, "%s", name);
        loadedSamples[loadedCount++] = samples;
    }
}
unsigned int plat_ticks_ms() { return 0; }
void plat_present(const unsigned short *, int, int, int) {}
unsigned int plat_pad() { return 0; }
void plat_music_play(const char *, int, unsigned int) {}
void plat_music_stop() {}
void plat_sfx_play(int, int) {}
void plat_log(const char *) {}
void plat_fatal(const char *) {}
bool plat_running() { return false; }
unsigned short *plat_layer_begin(int, int, int, unsigned short *frame) { return frame; }
void plat_layer_line(int, int, int) {}
void plat_layer_end(int) {}
int *plat_layer_lines(int) { return 0; }
void *plat_cart_data(int) { return calloc(1, 0x110000); }
void plat_stage_loaded() {}
void plat_stage_loading() {}
void plat_file_read(const char *, int) {}
void plat_draw_log(const char *, int, int) {}
void plat_sprite(int, int, int, int, int, int, int, int, int, int, int, int) {} // REV R3
unsigned short *plat_frame_target() { return 0; }
unsigned char *plat_fb_data(int, int size) { return (unsigned char *)calloc(1, size); }
void plat_chunk_arrays(const unsigned short **a, const unsigned char **b, const unsigned char **c, const unsigned char **d, int *n)
{
    *a = 0, *b = 0, *c = 0, d[0] = d[1] = 0, *n = 0;
}
unsigned char *plat_layout_pool(int *size)
{
    static unsigned char pool[96 * 1024];
    *size = sizeof(pool);
    return pool;
}

int main(int argc, char **argv)
{
    if (!CheckRSDKFile(argv[1])) {
        printf("cannot open pack\n");
        return 1;
    }
    Engine.LoadGameConfig("Data/Game/GameConfig.bin");
    for (int c = 0; c < 4; ++c)
        for (int s = 0; s < stageListCount[c]; ++s) printf("STAGE %d %d %s %s\n", c, s, stageList[c][s].folder, stageList[c][s].id);
    audioEnabled = true;
    LoadGlobalSfx();
    for (int i = 0; i < loadedCount; ++i) printf("SFX %s %d\n", loaded[i], loadedSamples[i]);
    Engine.bytecodeMode = BYTECODE_PC;
    scriptCodePos = 0;
    jumpTablePos  = 0;
    LoadBytecode(4, 0);
    printf("BYTECODE %d %d\n", scriptCodePos, jumpTablePos);
    return 0;
}
