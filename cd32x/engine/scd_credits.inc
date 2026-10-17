// REV R4: the credits screen - the Level Select menu's CREDITS line (engine/Debug.cpp calls plat_credits). Made after
// MicroNut99's Sonic32XPlus1-RealHardware (mars/menu.asm Mars_Credits, mars/sh2/master.c, plasma.c):
//   - the credits roll up from the bottom of the screen on Genesis plane B, a quarter pixel a frame: picture row r goes
//     into plane B's row (r + 28) & 31 just before it scrolls into view; the picture (G0S254.MDG, tools/build_game_data.py)
//     then repeats;
//   - OpenJazz's plasma behind them on the 32X (scd_plasma.h, unaltered), colours 160-175 without bit 15: behind the
//     Genesis picture, seen through its backdrop; drawn every frame into the back buffer, double-buffered;
//   - the CD player line on plane A rows 25-26.
// Keys as the template's: Up / Down plasma faster / slower, X the text (all but the CD line) off / on, B or Z back to the
// menu; Left / Right choose a track (2 to the disc's last audio track), A play, C stop, Start pause / go on. This port's
// Sub-CPU has play (command 45, the track repeated) and stop (54) only - no pause, no track count (no new boot-stack
// commands): pause stops the track and go on plays it again from its start, and the line shows the track number alone
// (the last track comes from engine/CDTracks.inc); its state is the CD drive's own (the Sub-CPU's heartbeat in COMM14:
// playing, paused, stopped, tray open ...).
// The records go where the 68000 reads them - the start of the frame buffer, the back buffer's line table: at most 250
// words, and the line table written again after each.
#define CREDITS_FILE 0x11FE /* G0S254.MDG */
#define CRDT_MAGIC 0x43524454u
#define CR_REC_MAX 250
#define SC_COMM14 (*(volatile unsigned short *)0x2000402E)
enum { CK_UP = 1, CK_DOWN = 2, CK_LEFT = 4, CK_RIGHT = 8, CK_B = 0x10, CK_C = 0x20, CK_A = 0x40, CK_START = 0x80, CK_Z = 0x100,
       CK_X = 0x400 };
static const int16_t crZero[SCREEN_YSIZE] = { 0 };
static const unsigned short crBlankRow[40] = { 0 };
static void cr_line_table(int words) /* the back buffer's line table (8-bit pixels: line y at word 0x100 + y * 160) */
{
    volatile unsigned short *fb = SC_FB;
    for (int y = 0; y < words && y < 256; ++y) fb[y] = (unsigned short)(0x100 + (y < 224 ? y : 223) * PL_LINE_WORDS);
}
static void cr_row(SgbOut *o, unsigned short plane, int row, const unsigned short *names)
{
    for (int c = 0; c < 40; ++c) sgb_write(o, (uint16_t)(plane + 2 * (row * 64 + c)), names[c]);
}
static unsigned short cr_rgb15(const unsigned char *c) { return (unsigned short)((c[2] >> 3) << 10 | (c[1] >> 3) << 5 | (c[0] >> 3)); }
/* the CD line: "TRACK 05  PLAYING" or why there is none, centred, in the picture's letters (two cells each) */
static void cr_cd_line(SgbOut *o, unsigned int glyphBase, unsigned int track, unsigned int player, unsigned int drive)
{
    char t[41], *p = t;
    const char *s;
    switch (drive) { /* the CD drive's status (the Sub-CPU's heartbeat, COMM14 bits 12-15) */
        case 5: s = "CD AUDIO  TRAY OPEN"; break;
        case 9: s = "CD AUDIO  READING THE DISC"; break;
        case 0xB: case 0xC: case 0xD: case 0xE: case 0xF: s = "CD AUDIO  NO DISC"; break;
        default: s = 0;
    }
    if (s)
        while (*s) *p++ = *s++;
    else {
        for (s = "TRACK "; *s;) *p++ = *s++;
        *p++ = (char)('0' + track / 10 % 10);
        *p++ = (char)('0' + track % 10);
        *p++ = ' ';
        *p++ = ' ';
        s = drive == 1 ? "PLAYING" : drive == 2 || drive == 3 ? "SEEKING" : drive == 4 || player == 2 ? "PAUSED" : "STOPPED";
        while (*s) *p++ = *s++;
    }
    *p = 0;
    int n = (int)(p - t), pre = (40 - n) / 2;
    for (int half = 0; half < 2; ++half) {
        unsigned short w[40];
        for (int c = 0; c < 40; ++c) {
            int k = c - pre, g = -1;
            char ch = k >= 0 && k < n ? t[k] : ' ';
            if (ch >= '0' && ch <= '9')
                g = ch - '0';
            else if (ch >= 'A' && ch <= 'Z')
                g = 10 + ch - 'A';
            w[c] = (unsigned short)(g < 0 ? 0 : 0x2000 | (glyphBase + 2 * (unsigned int)g + (unsigned int)half));
        }
        cr_row(o, SGB_NAME_A, 25 + half, w);
    }
}
void plat_credits()
{
    planes_off(); /* the Level Select menu's planes */
    menuDrawn = false;
    STATE(0x5C80);
    comm_idle();
    mdgInCart          = 0; /* REV R4B: the credits' file takes the act file's cart slot */
    unsigned int bytes = 0;
    int bad = ((ScdLoadFile)SCD_BOOT_RECORD[6])((unsigned short)CREDITS_FILE, &bytes);
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge the cache: the cart's contents changed */
    const unsigned char *f = SGB_FILE;
    unsigned int oc        = bad == 0 ? *(const unsigned int *)(f + 24) : 0;
    if (bad != 0 || *(const unsigned int *)f != SGB_MAGIC || *(const unsigned short *)(f + 4) != SGB_VERSION ||
        *(const unsigned int *)(f + 8) != bytes || oc + 12 > bytes || *(const unsigned int *)(f + oc) != CRDT_MAGIC) {
        STATE(bad < 0 ? 0x5C81 : 0x5C82); /* no credits file on the disc / not one: back to the menu */
        return;
    }
    const unsigned int rowsTotal = *(const unsigned short *)(f + oc + 4), glyphBase = *(const unsigned short *)(f + oc + 6);
    const unsigned short *map = (const unsigned short *)(f + *(const unsigned int *)(f + oc + 8));
    SC_COMM4 = 0;
    if (rowsTotal == 0 || sgb_cmd(1, SGB_CART_OFFSET) != 1) {
        STATE(0x5C83);
        return;
    }
    menuOn = true; /* the 68000 takes records now (planes_off ends it) */
    /* the 32X: 256 colours, priority bit clear; OpenJazz's colours without bit 15 - behind the Genesis picture */
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* REV R4B: the loading picture off first - the
                                                                         palette is written in blank */
    splashUp = false;
    for (int i = 0; i < 16; ++i) SC_CRAM[160 + i] = cr_rgb15(oj_pal[i]);
    for (int k = 0; k < 2; ++k) { /* both buffers: line table and a first plasma, before the 32X layer comes on */
        cr_line_table(256);
        plasma_draw(160);
        flip_wait();
    }
    MARS_VDP_DISPMODE = (unsigned short)((MARS_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256);
    STATE(0x5C84);
    unsigned int roll = 0, rows = 0, speed = 8, track = 2, player = 0, redraw = 0, drive = 0xFFFF, held = 0xFFFF;
    /* the disc's audio tracks: 2 (track 1 is the data track) to the last one tools/build_audio.py made (the template
       reads the count from the disc; this port knows it from engine/CDTracks.inc) */
    unsigned int lastTrack = 2;
#if SCD_HAVE_TRACKS
    for (int i = 0; i < scdTrackCount; ++i)
        if (scdTracks[i].track > lastTrack)
            lastTrack = scdTracks[i].track;
#else
    lastTrack = 99;
#endif
    unsigned int lastVb = VBLANK_COUNT;
    bool hide = false, lineDirty = true;
    for (;;) {
        plat_pad(); /* the pad request and answer (padRaw: SEGA_CTRL bits) */
        unsigned int raw     = (padRaw & 0xF000) == 0xF000 ? 0 : padRaw & 0x0FFF;
        unsigned int pressed = held == 0xFFFF ? 0 : raw & ~held; /* (the key that chose CREDITS is not a press here) */
        held                 = raw;
        if (pressed & (CK_B | CK_Z))
            break;
        if (pressed & CK_X) { /* the text (all but the CD line) off / on: the rows in plane B again */
            hide   = !hide;
            redraw = rows < 32 ? rows : 32;
        }
        else if (pressed & CK_UP) {
            if (speed < 16)
                speed++;
        }
        else if (pressed & CK_DOWN) {
            if (speed > 1)
                speed--;
        }
        else if (pressed & CK_START) { /* pause / go on - stop, then the track again from its start */
            if (player == 1) {
                sub_cmd(54, 0);
                player = 2;
            }
            else if (player == 2) {
                sub_cmd(45, (unsigned short)track);
                player = 1;
            }
            lineDirty = true;
        }
        else if (pressed & CK_LEFT) {
            track     = track > 2 ? track - 1 : lastTrack;
            lineDirty = true;
        }
        else if (pressed & CK_RIGHT) {
            track     = track < lastTrack ? track + 1 : 2;
            lineDirty = true;
        }
        else if (pressed & CK_A) {
            sub_cmd(45, (unsigned short)track);
            player = 1, creditsMusic = true, lineDirty = true;
        }
        else if (pressed & CK_C) {
            sub_cmd(54, 0);
            player = 0, lineDirty = true;
        }
        unsigned int d = SC_COMM14 >> 12;
        if (d != drive) {
            drive     = d;
            lineDirty = true;
        }
        plasma_draw(160); /* the back buffer's pixels (its line table after the record) */
        plasma_step((int)speed); /* once a frame drawn, as the template's master SH2 */
        /* the roll: a quarter pixel a vertical blank, upwards (the template's 68000 rolls it in its vertical blank; a
           plasma frame here takes about two) */
        unsigned int vb = VBLANK_COUNT;
        roll += 0x40 * (vb - lastVb);
        lastVb = vb;
        SgbOut o;
        sgb_out_begin(&o, SGB_FB_REC, CR_REC_MAX);
        unsigned int px = roll >> 8;
        int budget = lineDirty ? 3 : 5; /* rows in one record (43 words each: it must stay in the line table's 256) */
        for (int k = 0; k < 2 && rows * 8 <= px + 8; ++k, --budget) { /* the next picture rows, just below the screen */
            cr_row(&o, SGB_NAME_B, (int)((rows + 28) & 31), hide ? crBlankRow : map + (rows % rowsTotal) * 40);
            rows++;
        }
        for (; budget > 0 && redraw; --budget) { /* X: the rows in plane B again */
            unsigned int r = rows - redraw--;
            cr_row(&o, SGB_NAME_B, (int)((r + 28) & 31), hide ? crBlankRow : map + (r % rowsTotal) * 40);
        }
        if (lineDirty) {
            cr_cd_line(&o, glyphBase, track, player, drive);
            lineDirty = false;
        }
        int len = sgb_out_end(&o, 0, (uint16_t)(px & 0x3FF), crZero, crZero, SCREEN_YSIZE, 0, 0, 0);
        if (len > 0) {
            rec_handover();
            if (!menuOn)
                break; /* the 68000 did not take it (rec_handover switched the planes off) */
            cr_line_table(len + 1);
        }
        flip_wait(); /* shown from the next vertical blank */
    }
    MARS_VDP_DISPMODE = (unsigned short)(MARS_VDP_DISPMODE & 0x8000); /* the 32X layer off */
    planes_off();
    /* the frame buffer the engine keeps its menus, line and layouts in now holds plasma: cleared, as at power-on (the
       Level Select menu is made again; the layouts load with the next stage) */
    fb_clear(0x24000000, 0x20000);
    skyPicture = false; /* REV R4B: the 32X sky's picture was in a buffer the plasma used: made again for the next sky */
    STATE(0x5C8F);
}
