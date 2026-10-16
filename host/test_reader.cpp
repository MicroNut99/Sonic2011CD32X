// PORT32X: checks tools/rsdk_pack.py against the engine's own pack reader.
// usage: test_reader Data.rsdk expected_dir file1 file2 ...
// Loads each file with LoadFile()/FileRead() and compares with expected_dir/<file>.
#include "../engine/RetroEngine.hpp"
int main(int argc, char **argv)
{
    if (!CheckRSDKFile(argv[1])) {
        printf("cannot open pack %s\n", argv[1]);
        return 1;
    }
    int bad = 0;
    for (int a = 3; a < argc; a++) {
        FileInfo info;
        if (!LoadFile(argv[a], &info)) {
            printf("MISSING %s\n", argv[a]);
            bad++;
            continue;
        }
        byte *buf = (byte *)malloc(info.vFileSize + 1);
        FileRead(buf, info.vFileSize);
        CloseFile();
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", argv[2], argv[a]);
        FILE *f = fopen(path, "rb");
        byte *ref = (byte *)malloc(info.vFileSize + 1);
        int n = f ? (int)fread(ref, 1, info.vFileSize + 1, f) : -1;
        if (f)
            fclose(f);
        bool ok = n == info.vFileSize && !memcmp(buf, ref, n);
        printf("%s %s (%d bytes)\n", ok ? "OK  " : "DIFF", argv[a], info.vFileSize);
        bad += !ok;
        free(buf);
        free(ref);
    }
    printf("%d different\n", bad);
    return bad != 0;
}
