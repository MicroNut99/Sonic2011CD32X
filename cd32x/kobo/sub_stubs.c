#include <stdint.h>

// Sound / SPCM Stubs
void S_Init(void) {}
void S_Clear(void) {}
void S_Update(void) {}
void S_CopyBufferData(void) {}
void S_SetBufferData(void) {}
void S_PauseSPCMTrack(void) {}
void S_LoadCDBuffers(void) {}
int S_PlaySPCMTrack(const char *name, int repeat) { return 0; }
void S_StopSPCMTrack(void) {}
void S_UnpauseSPCMTrack(void) {}
void *S_GetMemBankPtr(void) { return (void*)0x600000; }
void stream_cd(int lba, int len) {}
int S_PlaySource(void) { return 0; }
void S_PUnPSource(void) {}
void S_RewindSource(void) {}
void S_StopSource(void) {}
void S_UpdateSource(void) {}
int S_GetSourcePosition(void) { return 0; }

// External assembly functions defined in crt.s
extern int find_dir_entry(char *name);
extern void ReadSectorsSUB(void *ptr, int lba, int len);
extern int set_cwd(char *path);
extern int next_dir_sec(void);
extern int DENTRY_OFFSET;
extern int DENTRY_LENGTH;

#define ERR_NO_MORE_ENTRIES -4
#define ERR_NAME_NOT_FOUND  -6

static int64_t open_file_disc(const char *name) {
    int r;

    /* Load the root directory sector into the shared disc buffer before
       searching it. Without this, find_dir_entry() searches whatever
       sector init_cd()'s own earlier PVD scan last read - not the root
       directory at all. Confirmed by direct comparison against a real,
       working open_file() implementation (cdfh.c). */
    r = set_cwd("/");
    if (r < 0)
        return -1;

    /* Loop across directory sectors on ERR_NAME_NOT_FOUND, matching the
       real reference exactly - the previous version gave up after
       checking only the first sector, which would incorrectly fail if
       the target file's entry doesn't happen to fall in that sector. */
    while (1) {
        r = find_dir_entry((char*)name);
        if (r >= 0)
            break;

        if (r == ERR_NAME_NOT_FOUND) {
            r = next_dir_sec();
            if (r == ERR_NO_MORE_ENTRIES)
                return -1;
        }

        if (r < 0)
            return r;
    }

    /* Return the file's real length and offset, populated by
       find_dir_entry() into these globals - not a hardcoded fake
       value. The previous version always returned length=1, offset=0
       regardless of what was actually found. */
    return ((int64_t)DENTRY_LENGTH << 32) | (uint32_t)DENTRY_OFFSET;
}

/* SONIC REV R4K - THE ROOT DIRECTORY IN SUB-CPU RAM. open_file_disc() reads the root directory from the disc one
   sector at a time, each read a new seek (CDCSTOP + ROMREADN): an open went through up to 8 sectors and took about
   1.7 s (measured in ares, 20 Oct; the console's IMAGE.RAW step agrees). Now the whole root directory is read ONCE, in
   one read, into dirc[] (the first open after power-on), and every open looks the name up there: no disc access.
   The disc cannot change while the game runs. The directory longer than dirc[], or its first sector not a directory
   ("." first): no cache, the old way (open_file_disc) for every open, as before. A name not in the cached root: -1,
   as the old way's answer (scd_open_file then skips its "/ROMS" search: dirc_root_has("ROMS") says there is none). */
extern char DISC_BUFFER[];
extern int CURR_OFFSET;
/* ReadSectorsSUB (d0 = first sector, d1 = count, a0 = buffer) called from C, as scd.c's scd_sub_read - defined here
   again because this file is also linked into crt.bin with crt.o alone (cd32x/Makefile), where scd.c is not */
extern void dirc_read_sectors(void *ptr, int lba, int len);
__asm__(
    "       .pushsection .text\n"
    "       .align  2\n"
    "       .globl  dirc_read_sectors\n"
    "dirc_read_sectors:\n"
    "       movea.l 4(%sp),%a0\n"
    "       move.l  8(%sp),%d0\n"
    "       move.l  12(%sp),%d1\n"
    "       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "       jsr     ReadSectorsSUB\n"
    "       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "       rts\n"
    "       .popsection\n"
);
#define DIRC_SECTORS 24
static uint8_t dirc[DIRC_SECTORS * 2048] __attribute__((aligned(4)));
static int dirc_state = 0;      /* 0: not tried yet, 1: in RAM, -1: not usable (the old way) */
static uint32_t dirc_secs = 0;  /* the root directory's sectors in dirc[] */
static uint32_t be32_at(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void dirc_load(void) {
    uint32_t lba, len, n;
    const uint8_t *d = (const uint8_t *)DISC_BUFFER;
    dirc_state = -1;
    if (set_cwd("/") < 0)                         /* the root's first sector into DISC_BUFFER, its LBA in CURR_OFFSET */
        return;
    if (d[0] < 34 || d[32] != 1 || d[33] != 0)   /* the first record must be "." (name length 1, name byte 0) */
        return;
    lba = be32_at(d + 6);                         /* "." = the root itself: its extent and length (big-endian halves) */
    len = be32_at(d + 14);
    if (lba != (uint32_t)CURR_OFFSET || len == 0)
        return;
    n = (len + 2047) / 2048;
    if (n > DIRC_SECTORS)
        return;
    dirc_read_sectors(dirc, (int)lba, (int)n);    /* one read: one seek */
    if (dirc[0] != d[0] || be32_at(dirc + 6) != lba)
        return;                                   /* (not the same first sector: no cache) */
    dirc_secs = n;
    dirc_state = 1;
}
static int dirc_find(const char *name, uint32_t *extent, uint32_t *length) {
    uint32_t s, o, nl, i;
    for (s = 0; s < dirc_secs; s++) {
        const uint8_t *sec = dirc + s * 2048;
        for (o = 0; o + 34 <= 2048 && sec[o] != 0;) {
            const uint8_t *r = sec + o;
            uint32_t rl = r[0];
            if (rl < 34 || o + rl > 2048)
                break;                            /* a bad record: the rest of this sector skipped */
            nl = r[32];
            if (nl > 0 && !(nl == 1 && r[33] <= 1)) { /* not "." / ".." */
                for (i = 0; i < nl && r[33 + i] != ';' && name[i] != 0 && r[33 + i] == (uint8_t)name[i]; i++) {
                }
                if ((i == nl || r[33 + i] == ';') && name[i] == 0) {
                    *extent = be32_at(r + 6);
                    *length = be32_at(r + 14);
                    return 1;
                }
            }
            o += rl;
        }
    }
    return 0;
}
int dirc_root_has(const char *name) { /* 1: in the cached root, 0: not there, -1: no cache */
    uint32_t e, l;
    if (dirc_state != 1)
        return -1;
    return dirc_find(name, &e, &l);
}
int64_t open_file(const char *name) {
    uint32_t extent, length;
    if (dirc_state == 0)
        dirc_load();
    if (dirc_state != 1)
        return open_file_disc(name);
    if (!dirc_find(name, &extent, &length))
        return -1;
    return ((int64_t)length << 32) | extent;
}

int64_t read_directory(char *path) {
    return -1;
}

void read_sectors(void *ptr, int lba, int len) {
    ReadSectorsSUB(ptr, lba, len);
}
