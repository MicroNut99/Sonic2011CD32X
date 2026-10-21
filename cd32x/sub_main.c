#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw_md.h"
#include "files.h"

uint16_t gen_lvl2 = 0;

void write_byte(unsigned int dst, unsigned char val) { *(volatile unsigned char *)dst = val; }
void write_word(unsigned int dst, unsigned short val) { *(volatile unsigned short *)dst = val; }
void write_long(unsigned int dst, unsigned int val) { *(volatile unsigned int *)dst = val; }
unsigned char read_byte(unsigned int src) { gen_lvl2++; return *(volatile unsigned char *)src; }
unsigned short read_word(unsigned int src) { return *(volatile unsigned short *)src; }
unsigned int read_long(unsigned int src) { return *(volatile unsigned int *)src; }
void scd_init_pcm(void) {}
void bump_fm(void) { gen_lvl2++; }

int mystrlen(const char* string) {
    int rc = 0;
    while (*(string++)) rc++;
    return rc;
}

extern int sh2_app_length;
extern char sh2_app_start[];
extern char Sub_Start[];
extern char Sub_End[];
extern uint16_t InitCD(void);
extern volatile int scd_last_timeout_code;

extern int64_t scd_open_file(const char *name);
extern void scd_read_sectors(void *ptr, int lba, int len, void (*wait)(void));
extern void scd_stream_cmd(char cmd); 
extern void scd_sub_read(void *ptr, int lba, int len);
extern int  scd_cd_status(void);                           
extern int  scd_track_info(int track);                     
extern void scd_cdda_play(int track);                       
extern int  scd_bios_call(int fn, int d1, void *a0);       

/* 68K VBlank Tick Macro. Sub-CPU accesses Main CPU Comm Registers via 0xFF8000 */
#define GET_TICKS read_long(0xFF801C)

#define SUB_MAIN_FLAG  0xFF800E
#define SUB_SUB_FLAG   0xFF800F

static void fourcc(char *out, unsigned int v)
{
    int i, ok = 1;
    for (i = 0; i < 4; i++) {
        unsigned char c = (v >> (24 - 8 * i)) & 0xFF;
        if (c < 0x20 || c > 0x7E) ok = 0;
    }
    if (ok) {
        for (i = 0; i < 4; i++) out[i] = (v >> (24 - 8 * i)) & 0xFF;
        out[4] = 0;
    } else {
        sprintf(out, "%08X", v);
    }
}

extern int do_md_cmd0(int cmd);
extern int do_md_cmd1(int cmd, int arg1);
extern int do_md_cmd2(int cmd, int arg1, int arg2);
extern int do_md_cmd3(int cmd, int arg1, int arg2, int arg3);

#define MD_CMD_CLEAR_B        4
#define MD_CMD_CLEAR_A        8
#define MD_CMD_READ_32X_16    19
#define MD_CMD_WRITE_32X_16   20
#define MD_CMD_READ_32X_32    21
#define MD_CMD_WRITE_32X_32   22
#define MD_CMD_COPY_FROM_CART 24
#define MD_CMD_COPY_WORDS     25
#define MD_CMD_SAFE_CART_XFER 26   
#define MD_CMD_SAFE_CART_XFER_W 27     
#define MD_CMD_CHILLY_MEMCPY  32         
#define MD_CMD_CHILLY_RV_MEMCPY 33       
#define MD_CMD_BRAM_PUT       28         
#define MD_CMD_BRAM_TO_FB     29         
#define MD_CMD_BRAM_GET       30         
#define MD_CMD_INSTALL_EXC    31         /* HW1: 68K exception trap (K29), prints 68K EXC in red */
#define MD_CMD_SET_VDP_REG    34         /* KOBO K19: hw_md.s set_vdp_reg - one VDP register word */

/* KOBO K19 - GREEN DIAGNOSTIC TEXT.  1 = printed as before (C on the pad shows/hides it).
   0 = print_diag() prints nothing at all: every print_diag() call stays in the code, so
   setting 1 again brings all the text back.  (Chilly's white 32X start-up messages come
   from hw_md.s and are not affected.) */
#define KOBO_DIAG_TEXT        1          /* SONIC CD S3B4: on (diagnosis); green colour restored in kobo/hw_md.s */

/* K30 - BOOT TEXT.  0 = the Genesis display is switched OFF at the very start of main(), so
   neither the green text nor Chilly's white 32X start-up messages appear (dev request): the
   SEGA logo is followed by the splash.  The text is still written to video RAM, so setting 1
   (or pressing C in the game) shows it.  Set 1 when debugging a start-up problem. */
#define KOBO_HW_QUIET         0          /* HW9: 1 = once the game reports 4B10, the Sub-CPU stops polling
                                                   the 68K; it only writes its heartbeat twice a second. */
static int sub_quiet = 0;
/* SONIC 2011 REV R1 - the Genesis planes (md/sgb.h, cd32x/kobo/hw_md.s sgb_*). SH2 command 68:
 *   COMM2 = 1 load: COMM8 (32-bit) = cart offset of the act's "SCMG" file (G<list>S<pos>.MDG, loaded
 *             by the SH2 with CMD 38/46 into slot 28 = cart 0x380000). VDP set up for the planes,
 *             the act's cells and colours from the cart (68000: 32X bank window 0x900000), planes
 *             cleared, then the listener's record handling on (SGB_FLAG). Reply COMM12 1 ok, 0 bad.
 *   COMM2 = 2 off:  records off, the listener's own VDP set-up, font and text back (md_init_hw).
 * While the planes are on nothing may print into the planes: no Kobo text (print_diag). */
#define SGB_CMD          68
#define SGB_SLOT         28              /* CMD 46 slot: 28 * CART_SLOT = cart 0x380000 */
#define SGB_FLAG_ADDR    0xFF8000        /* 68000 work RAM - the listener's SGB_ equates */
#define SGB_LAST_ADDR    0xFF8002
#define SGB_READY_ADDR   0xFF8004
#define MD_CMD_INIT_HW_  1               /* hw_md.s command numbers (md_init_hw, copy_vram, set_palette) */
#define MD_CMD_COPY_VRAM_ 7
#define MD_CMD_SET_PALETTE_ 12
static int planes_on = 0;
static int floor_on = 0;                  /* SONIC REV R5: 1 - Word RAM in 2 Mbit mode, the floor in it (command 80) */
#define KOBO_BOOT_TEXT        1          /* K31: on - Chilly's white 32X start-up messages show
                                                   (green text stays off: KOBO_DIAG_TEXT 0) */

#define LEGACY_68K_CART 0

static uint32_t saved_lba = 0;
/* K10: length of the file opened by CMD 38, kept here (like saved_lba) so CMD 50 never has
   to trust COMM8 - in K9 CMD 50 read a wrong length from COMM8 and read 0 sectors. */
static uint32_t saved_len = 0;
static int quiet = 0;      
#define SLIDESHOW_DEBUG 1

static const char *find_sh2_marker(const char *p, int len)
{
    int i;
    for (i = 0; i + 8 <= len; i++)
        if (p[i] == 'S' && p[i+1] == 'H' && p[i+2] == '2' && p[i+3] == '-' && p[i+4] == 'V')
            return p + i + 5;
    return 0;
}

void print_diag(char *temp, const char *msg, int x, int y) {
    if (quiet) return;
    if (planes_on) return;                    /* SONIC REV R1: the planes use that video RAM */
    if (floor_on) return;                     /* SONIC REV R5: Word RAM is the floor's (2 Mbit mode) */
#if !KOBO_DIAG_TEXT
    return;                                   /* K19: green text switched off (see top) */
#endif
    strcpy(temp, msg);
    switch_banks();
    do_md_cmd4(MD_CMD_PUT_STR, 0x200000, TEXT_GREEN, x, y);
    switch_banks();
}

/* =====================================================================================
 * KOBO STORAGE MODE - must match d32xr-master/sh2_main.c KOBO_NOCART
 *
 *   1 = NO-CART (development / Fusion): the RAM cart is never touched - not even the
 *       start-up cart check (accessing a missing cart crashes the console).  The SH2 reads
 *       KOBOGFX.BIN straight from CD with CMD 38 (open) + CMD 50 (one 32 KB chunk into the
 *       frame buffer).  Runs in Fusion and on the console, cart plugged in or not.
 *   0 = CART (real hardware + MicroNut99 cart): KOBOGFX.BIN is stored in the cart at
 *       offset 2 MB with CMD 38 + CMD 46 (Chilly's rules, verified), the SH2 reads it at
 *       0x22000000.  Proven on hardware.
 * ===================================================================================== */
#define KOBO_NOCART     0          /* SONIC CD S3B4: RAM cart hardware build (Micronut99). FUSION build: 1 */

#define CART_TARGET     0              /* MicroNut99 cart layout (only used when KOBO_NOCART 0) */
#define KOBO_PACK_NAME  "KOBOGFX.BIN"
#define KOBO_PACK_SLOT  16             /* cart mode: 16 * CART_SLOT = 2 MB into the 4 MB cart */
#define CART_WINDOW_READS    1   
#define CART_VERIFY_READBACK 1   

#define CART_IMG_BASE   0x000000
#define CART_SLOT       0x20000
#define CMD_CHUNK_TO_CART 46
#define CHUNK_RUN_MAX     4              /* SONIC REV R4C: chunks per fast CMD 46 - this side's 128 KB Word RAM bank */
#define CMD_CART_TO_FB    47
/* SONIC REV R4C: the fast CMD 46's checksum of the bytes read (the SH2 computes the same over the cart, as big-endian
   32-bit words: cd32x/engine/plat_mars.cpp, cd32x/sh2_engboot.c load_sum) */
static uint32_t load_sum(const uint32_t *p, uint32_t longs)
{
    uint32_t a = 0, b = 0;
    while (longs--) {
        a += *p++;
        b += a;
    }
    return a ^ ((b << 13) | (b >> 19));
}
static int cur_img = 0;
static int g_nexc = 0;                   

#define XB(off)   (0x0C0100 + (off))       
#define XM(off)   (0x200100 + (off))       
#define PB(off)   (*(volatile uint8_t *)XB(off))

static void cx(int src, int dst, int len)  
{
    do_md_cmd3(MD_CMD_SAFE_CART_XFER, src, dst, len);
}

static int safe_cart_present(char *temp, char *tbuf)
{
    int ok;
#if CART_TARGET == 1
    uint8_t id, orig, rb;
    PB(0) = 0; PB(1) = 0; PB(2) = 0x5A; PB(3) = 0; PB(4) = 1; PB(5) = 0;
    switch_banks();                              
    cx(0x400001, XM(0), 1);                      
    cx(0x600001, XM(1), 1);                      
    cx(XM(4), 0x7FFFFF, 1);                      
    cx(XM(2), 0x600001, 1);                      
    cx(0x600001, XM(3), 1);                      
    cx(XM(1), 0x600001, 1);                      
    cx(XM(5), 0x7FFFFF, 1);                      
    switch_banks();                              
    id = PB(0); orig = PB(1); rb = PB(3);
    ok = (rb == 0x5A);
    sprintf(tbuf, "BRAM ID:%02X (%uKB) OLD:%02X        ", id, (id & 0x80) ? 0 : (0x2000u << (id & 7)) >> 10, orig);
    print_diag(temp, tbuf, 2, 23);
    sprintf(tbuf, "BRAM @600001: WROTE 5A READ %02X  ", rb);
#else
    uint16_t orig = 0, via9 = 0xFFFF, via4 = 0xFFFF;
    PB(0) = 0x5A; PB(1) = 0xA5; PB(2) = 0; PB(3) = 0;
#if CART_WINDOW_READS
    do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, 0);                       
    orig = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0x900000);
#endif
    switch_banks();
    do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x400000, XM(0), 2);          
    switch_banks();                                                     
    via4 = 0xFFFF;                                                      
#if CART_WINDOW_READS
    via9 = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0x900000);        
    PB(0) = orig >> 8; PB(1) = orig & 0xFF;
    switch_banks();
    do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x400000, XM(0), 2);          
    switch_banks();
#endif
#if !CART_WINDOW_READS && !CART_VERIFY_READBACK
    ok = 1;                                                             
#else
    ok = (via4 == 0x5AA5 || via9 == 0x5AA5);
#endif
    sprintf(tbuf, "OLD:%04X  VIA 400000:%04X      ", orig, via4);
    print_diag(temp, tbuf, 2, 23);
    sprintf(tbuf, "WROTE 5AA5  VIA 900000:%04X    ", via9);
#endif
    print_diag(temp, tbuf, 2, 24);
    print_diag(temp, ok ? "SAFE CART: FOUND               " : "SAFE CART: NOT FOUND           ", 2, 25);
    return ok;
}

static uint16_t cdda_track_word;       

static int bios_wait_ready(unsigned int max_ticks)
{
    (void)max_ticks;
    while (scd_cd_status() & 0x80000000) {
        bump_fm();
    }
    return 1;
}


/* =====================================================================================
 * K33 SOUND EFFECTS - Sega CD PCM chip (RF5C164), driven directly by the Sub-CPU.
 *   KOBOSFX.BIN (tools/kobo_sfx.py, from Kobo's own rendered .agw sounds): a table + a
 *   ready-made 64 KB wave-RAM image, loaded ONCE at boot by CMD 53 (before the music:
 *   no CD reads after the music starts).
 *   Playing: the SH2 puts an effect number in the upper byte of its pad request
 *   (COMM0 = 48 | id << 8); the fast path below starts it - no extra exchange, no 68K.
 *   Channel 7 = the player's gun (restarted each shot), channels 0-6 = round robin.
 *   Chip registers (odd bytes at 0xFF0001..): ENV volume, PAN, FDL/FDH step (0x800 = 32552
 *   Hz), LSL/LSH loop address, ST start page, CTRL (bit 7 sound on, bit 6: 1 = choose a
 *   channel for the registers above, 0 = choose a 4 KB wave-RAM bank), ONOFF (bit = 1 off).
 *   Wave RAM window: 0xFF2001 + 2 * n, n < 4096, of the bank chosen in CTRL.
 * ===================================================================================== */
#define PCM_ENV    0xFF0001
#define PCM_PAN    0xFF0003
#define PCM_FDL    0xFF0005
#define PCM_FDH    0xFF0007
#define PCM_LSL    0xFF0009
#define PCM_LSH    0xFF000B
#define PCM_ST     0xFF000D
#define PCM_CTRL   0xFF000F   /* K35b FIX: register 7 (was 0xFF0011 = the ON/OFF register!)    */
#define PCM_ONOFF  0xFF0011   /* K35b FIX: register 8 (was 0xFF0013 - nothing there).  Order of
                                 the registers: ENV PAN FDL FDH LSL LSH ST CTRL ON/OFF at
                                 0xFF0001, 03 ... 11.  K33/K35 wrote the channel/bank selects
                                 into ON/OFF: channels switched on/off at random and never
                                 stopped cleanly - the shot kept looping (seen in Fusion). */
#define PCM_WAVE   0xFF2001
#define SFX_MAX    63         /* SONIC REV R4D: 63 (the scenes' own banks; R4C 31 - KOBOSFX.BIN has 23) */
typedef struct { uint8_t st, vol; uint16_t fd, loop, len; } sfx_t;
static sfx_t sfx_tab[SFX_MAX + 1];            /* [1..count] */
static int sfx_count = 0;
static uint8_t pcm_onoff = 0xFF;              /* shadow: the ONOFF register cannot be read */
static int pcm_next = 0;

static void pcm_play(int id) {
    const sfx_t *e;
    int ch;
    if (id < 1 || id > sfx_count) return;
    e = &sfx_tab[id];
    ch = (id == 1) ? 7 : pcm_next;            /* id 1 = player gun: its own channel */
    if (id != 1) pcm_next = pcm_next < 6 ? pcm_next + 1 : 0;
    pcm_onoff |= (uint8_t)(1 << ch);          /* key off (restart from the start page) */
    write_byte(PCM_ONOFF, pcm_onoff);
    write_byte(PCM_CTRL, 0xC0 | ch);          /* sound on + registers of channel ch */
    write_byte(PCM_ENV, e->vol);
    write_byte(PCM_PAN, 0xFF);
    write_byte(PCM_FDL, e->fd & 0xFF);
    write_byte(PCM_FDH, e->fd >> 8);
    write_byte(PCM_LSL, e->loop & 0xFF);
    write_byte(PCM_LSH, e->loop >> 8);
    write_byte(PCM_ST, e->st);
    pcm_onoff &= (uint8_t)~(1 << ch);         /* key on */
    write_byte(PCM_ONOFF, pcm_onoff);
}

/* =====================================================================================
 * SONIC REV R5 - THE SPECIAL STAGES' FLOOR ON THE SEGA CD'S GRAPHICS CHIP (SH2 command 80)
 *   The 1993 game's way: the rotating floor is drawn by the Sega CD's stamp / rotation chip into Word RAM (2 Mbit
 *   mode), and the 68000 puts it into the Genesis video RAM, where the window plane shows it under the 32X picture
 *   (the 32X's own pixels of the floor's rows are colour 0: see-through). tools/build_floor.py makes FLOOR<n>.BIN
 *   (stamps at 0x00000, the stamp map at 0x20000 - the file is the Word RAM image).
 *   COMM2 low byte = op:
 *     0 begin  (COMM2 high byte = special stage 1-8): Word RAM to 2 Mbit mode, the file into it, the chip and the
 *              Genesis set up (window plane rows 15-27 = lines 120-223, colours line 3). Reply COMM12 1 ok / 0 bad.
 *     1 / 2 / 3  COMM8 = the floor layer's X / Z (the engine's XPos >> 4, ZPos >> 4) / its YPos
 *     4 frame  COMM8 = sin << 16 | cos (the engine's sinM / cosM of the layer's angle): the floor drawn from 1-4;
 *              if the last one is still on its way, the newest values wait and go next.
 *     5 colours (COMM2 high byte = colour k 1-15): COMM8 = the Genesis colours of k (high word) and k + 1
 *     6 the floor file's palette index of colour k (COMM2 high byte): reply COMM8
 *     7 end    the Genesis display off and its set-up back (as command 68's off), Word RAM back to 1 Mbit mode.
 *   The work after a frame - the chip (~13 ms), the copy into video RAM (16.6 KB, in 2 KB pieces), the switch of
 *   the window's name table before line 120 - runs between the other commands (floor_step), so the pad's
 *   command 48 is still answered every frame. While the floor is on, nothing else uses Word RAM: no green text, and
 *   any command that does (loads, sounds banks, backup RAM) ends the floor first. */
#define FLOOR_CMD     80
#define FLR_SUB       0x080000            /* Word RAM in 2 Mbit mode as the Sub-CPU sees it ... */
#define FLR_MAIN      0x200000            /* ... and the 68000 */
#define FLR_IMG       0x10000             /* the chip's image buffer: 40 columns of 13 cells (column after column) */
#define FLR_TRACE     0x18000             /* the trace table: 104 lines x 4 words */
#define FLR_TABLE     0x1C000             /* (at the start only) the two window name tables, then a zero block */
#define FLR_ZERO      0x1E000
#define FLR_LINES     104
#define FLR_CELLS     (40 * 13)
#define FLR_WORDS     (FLR_CELLS * 16)
#define FLR_PIECE     1024                /* words copied into video RAM a pass */
static const uint16_t flr_vram[2] = { 0x0000, 0x4400 };   /* the two image buffers in video RAM */
static const uint16_t flr_win[2]  = { 0xA000, 0xB000 };   /* their window name tables */
static int floor_busy = 0;                /* 0 idle, 1 the chip drawing, 2 the copy, 3 the switch */
static int floor_pend = 0;                /* new values came while busy */
static int floor_buf = 0;                 /* the video RAM buffer being written (the other one is shown) */
static int floor_done = 0;                /* words copied */
static int32_t flr_x, flr_z, flr_y, flr_sin, flr_cos;
static uint32_t flr_tick;

static void flr_to_sub(void)              /* (2 Mbit) the 68000 gives Word RAM to the Sub-CPU: DMNA */
{
    int w = do_md_cmd1(MD_CMD_READ_32X_16, 0xA12002);
    do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA12002, (w & 0xFF00) | 0x02);
    while (read_byte(0xFF8003) & 1) bump_fm();      /* RET 0: the Sub-CPU's */
}
static void flr_to_main(void)             /* (2 Mbit) the Sub-CPU gives it to the 68000: RET */
{
    write_byte(0xFF8003, (read_byte(0xFF8003) & 0x18) | 0x01);
    while (!(read_byte(0xFF8003) & 1)) bump_fm();
}
static uint16_t flr_s16(int32_t v) { return (uint16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static void floor_trace(void)             /* the engine's Draw3DSkyLayer rows (pm_sky_part), one per screen line */
{
    volatile uint16_t *t = (volatile uint16_t *)(FLR_SUB + FLR_TRACE);
    int k;
    for (k = 0; k < 4; k++, t += 4) {     /* lines 120-123: above the floor - outside the map */
        t[0] = 0xFFF8; t[1] = 0xFFF8; t[2] = 0; t[3] = 0;
    }
    for (k = 0; k < FLR_LINES - 4; k++, t += 4) {   /* line 124 + k: the engine's sample row 9 + 2k, sample 1 */
        int32_t q  = flr_y / ((9 + 2 * k) << 8);
        int32_t xb = (q * -flr_cos) >> 9, yb = (flr_sin * q) >> 9;
        int32_t X  = flr_x + ((3 * flr_sin * q) >> 2) - xb * 320 + xb;
        int32_t Y  = flr_z + ((3 * flr_cos * q) >> 2) - yb * 320 + yb;
        int32_t X2 = X + 2 * 319 * xb, Y2 = Y + 2 * 319 * yb;   /* (the line's last pixel) */
        /* the chip's positions are 13 bits of pixels: a line that leaves -4096 .. 8191 would come round into the map
           (the engine: outside, see-through) - such a line (far, at the top of the floor) stays see-through */
        if (xb > 32767 || xb < -32768 || yb > 32767 || yb < -32768 ||
            X < -(4096 << 12) || X >= (8192 << 12) || Y < -(4096 << 12) || Y >= (8192 << 12) ||
            X2 < -(4096 << 12) || X2 >= (8192 << 12) || Y2 < -(4096 << 12) || Y2 >= (8192 << 12)) {
            t[0] = 0xFFF8; t[1] = 0xFFF8; t[2] = 0; t[3] = 0;
            continue;
        }
        t[0] = (uint16_t)(X >> 9);        /* 12 bits of fraction -> 13.3 */
        t[1] = (uint16_t)(Y >> 9);
        t[2] = flr_s16(xb);               /* a pixel = two samples of 1/4096: xb / 2048 (5.11) */
        t[3] = flr_s16(yb);
    }
}
static void floor_start(void)             /* the chip: this frame's floor into the image buffer */
{
    floor_trace();
    write_word(0xFF8058, 0x0004);         /* stamps 16 x 16, map 4096 x 4096, no repeat */
    write_word(0xFF805A, 0x20000 >> 2);   /* the map at 0x20000 */
    write_word(0xFF805C, 12);             /* 13 cells high */
    write_word(0xFF805E, FLR_IMG >> 2);
    write_word(0xFF8060, 0);
    write_word(0xFF8062, 320);
    write_word(0xFF8064, FLR_LINES);
    write_word(0xFF8066, FLR_TRACE >> 2); /* (starts it) */
    floor_busy = 1;
    floor_pend = 0;
}
static void floor_step(void)              /* one piece of the work after a frame (the main loop, every pass) */
{
    if (!floor_on || !floor_busy)
        return;
    if (floor_busy == 1) {
        if (read_word(0xFF8058) & 0x8000)
            return;                       /* the chip still draws */
        flr_to_main();
        floor_done = 0;
        floor_busy = 2;
        return;
    }
    if (floor_busy == 2) {
        int n = FLR_WORDS - floor_done;
        if (n > FLR_PIECE) n = FLR_PIECE;
        do_md_cmd3(MD_CMD_COPY_VRAM_, flr_vram[floor_buf] + 2 * floor_done, FLR_MAIN + FLR_IMG + 2 * floor_done, n);
        floor_done += n;
        if (floor_done >= FLR_WORDS) {
            floor_busy = 3;
            flr_tick = GET_TICKS;
        }
        return;
    }
    /* 3: the window shows the new buffer from the next vertical blank on (any time before line 120: no tear) */
    if (GET_TICKS == flr_tick)
        return;
    do_md_cmd1(MD_CMD_SET_VDP_REG, 0x8300 | (flr_win[floor_buf] >> 10));
    floor_buf ^= 1;
    flr_to_sub();
    floor_busy = 0;
    if (floor_pend)
        floor_start();
}
static void floor_end(void)
{
    if (!floor_on)
        return;
    while (floor_busy == 1 && (read_word(0xFF8058) & 0x8000)) bump_fm();
    if (floor_busy == 0 || floor_busy == 1)
        flr_to_main();                    /* (busy 2 / 3: the 68000 has it already) */
    floor_busy = 0;
    do_md_cmd1(MD_CMD_SET_VDP_REG, 0x8134);           /* the Genesis display off */
    write_byte(0xFF8003, 0x05);           /* Word RAM: 1 Mbit mode again (RET 1, as init_hardware leaves it) */
    do_md_cmd0(MD_CMD_INIT_HW_);          /* the listener's VDP set-up, font, colours (as command 68's off) */
    do_md_cmd0(MD_CMD_CLEAR_B);
    do_md_cmd0(MD_CMD_CLEAR_A);
    floor_on = 0;
}
static int floor_begin(int stage)
{
    char name[16];
    int64_t fi;
    uint32_t lba, len;
    int i, r, c;
    if (floor_on)
        floor_end();
    if (stage < 1 || stage > 8)
        return 0;
    sprintf(name, "FLOOR%d.BIN", stage);
    fi = scd_open_file(name);
    if (fi < 0)
        return 0;
    len = (uint32_t)(fi >> 32); lba = (uint32_t)(fi & 0xFFFFFFFF);
    if (len != 0x40000)
        return 0;
    floor_on = 1;                         /* (from here: no green text - it would use Word RAM) */
    write_byte(0xFF8003, read_byte(0xFF8003) & 0x19);   /* MODE 0: 2 Mbit */
    flr_to_sub();
    {   /* the window name tables (rows 15-27: the image's cells, column after column, colours line 3) and zeros */
        volatile uint16_t *t;
        int b;
        for (b = 0; b < 2; b++) {
            t = (volatile uint16_t *)(FLR_SUB + FLR_TABLE + b * 0x1000);
            for (i = 0; i < 64 * 32; i++) t[i] = 0;
            for (r = 0; r < 13; r++)
                for (c = 0; c < 40; c++)
                    t[(15 + r) * 64 + c] = (uint16_t)(0x6000 | ((flr_vram[b] >> 5) + c * 13 + r));
        }
        t = (volatile uint16_t *)(FLR_SUB + FLR_ZERO);
        for (i = 0; i < 0x1000; i++) t[i] = 0;
    }
    flr_to_main();
    {
        static const uint16_t regs[] = {
            0x8134,                       /* display off while it is set up */
            0x8004, 0x8230,               /* plane A 0xC000 */
            0x8328, 0x8407,               /* window 0xA000, plane B 0xE000 */
            0x857C, 0x8D3F,               /* sprites 0xF800, h-scroll 0xFC00 */
            0x8700, 0x8AFF, 0x8B00,       /* backdrop colour 0, full-screen scroll */
            0x8C81, 0x8E00, 0x8F02,       /* H40, no shadow / highlight, auto-increment 2 */
            0x9001, 0x9100, 0x928F        /* planes 64 x 32; the window: rows 15 to the bottom */
        };
        for (i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); i++)
            do_md_cmd1(MD_CMD_SET_VDP_REG, regs[i]);
        do_md_cmd3(MD_CMD_COPY_VRAM_, 0xA000, FLR_MAIN + FLR_TABLE, 0x800);           /* window tables */
        do_md_cmd3(MD_CMD_COPY_VRAM_, 0xB000, FLR_MAIN + FLR_TABLE + 0x1000, 0x800);
        for (i = 0; i < 0x8800; i += 0x1000)                                         /* both image buffers */
            do_md_cmd3(MD_CMD_COPY_VRAM_, i, FLR_MAIN + FLR_ZERO, 0x800);
        do_md_cmd3(MD_CMD_COPY_VRAM_, 0xC000, FLR_MAIN + FLR_ZERO, 0x800);            /* plane A, plane B */
        do_md_cmd3(MD_CMD_COPY_VRAM_, 0xE000, FLR_MAIN + FLR_ZERO, 0x800);
        do_md_cmd3(MD_CMD_COPY_VRAM_, 0xF000, FLR_MAIN + FLR_ZERO, 0x800);            /* sprites, h-scroll */
        do_md_cmd2(MD_CMD_WRITE_32X_32, 0xC00004, 0x40000010);                       /* v-scroll 0 */
        do_md_cmd2(MD_CMD_WRITE_32X_32, 0xC00000, 0);
        do_md_cmd2(MD_CMD_WRITE_32X_32, 0xC00004, 0xC0000000);                       /* the colours black */
        for (i = 0; i < 32; i++) do_md_cmd2(MD_CMD_WRITE_32X_32, 0xC00000, 0);
    }
    flr_to_sub();
    scd_sub_read((void *)FLR_SUB, (int)lba, 0x40000 / 2048);   /* the file: the stamps and the map */
    floor_buf = 0;
    floor_busy = 0;
    floor_pend = 0;
    do_md_cmd1(MD_CMD_SET_VDP_REG, 0x8300 | (flr_win[1] >> 10));   /* (buffer 1 shown: empty) */
    do_md_cmd1(MD_CMD_SET_VDP_REG, 0x8174);                         /* the Genesis display on */
    return 1;
}

/* CMD 53: KOBOSFX.BIN -> PCM wave RAM.  Returns the number of effects (0 = failed).
   SONIC REV R4D: bank (COMM2) 0 = KOBOSFX.BIN, 1-99 = SFX<bank>.BIN (a scene's own effects, tools/build_audio.py).
   The table's 8-byte rows follow the 8-byte header; the image starts at the next 256 bytes ((8 + n * 8 + 255) & ~255:
   256 for KOBOSFX.BIN, as before).  A file that is not there or not right changes nothing (0): the chip keeps what
   it had. */
static char pcm_bank_name[] = "SFX00.BIN";
static int pcm_load(int bank) {
    int64_t fi;
    uint32_t len, lba, nsec, img, hdr, i, b, n;
    volatile uint8_t *buf = (volatile uint8_t *)0x0C0000;   /* Sub-CPU's Word RAM bank */
    if (bank > 0 && bank < 100) {
        pcm_bank_name[3] = (char)('0' + bank / 10);
        pcm_bank_name[4] = (char)('0' + bank % 10);
        fi = scd_open_file(pcm_bank_name);
    } else
        fi = scd_open_file("KOBOSFX.BIN");
    if (fi < 0) return 0;
    len = (uint32_t)(fi >> 32); lba = (uint32_t)(fi & 0xFFFFFFFF);
    nsec = (len + 2047) / 2048;
    if (nsec > 48) nsec = 48;
    scd_sub_read((void *)0x0C0000, lba, nsec);
    if (buf[0] != 'K' || buf[1] != 'S' || buf[2] != 'F' || buf[3] != 'X') return 0;
    n = (uint32_t)((buf[4] << 8) | buf[5]);
    img = (uint32_t)((buf[6] << 8) | buf[7]);
    if (img == 0) img = 65536;
    if (n == 0 || n > SFX_MAX) return 0;
    hdr = (8 + n * 8 + 255) & ~255u;
    if (hdr + img > nsec * 2048) return 0;                    /* (a short file: not loaded) */
    sfx_count = (int)n;
    for (i = 0; i < (uint32_t)sfx_count; i++) {
        volatile uint8_t *t = buf + 8 + i * 8;
        sfx_tab[i + 1].st = t[0]; sfx_tab[i + 1].vol = t[1];
        sfx_tab[i + 1].fd = (uint16_t)((t[2] << 8) | t[3]);
        sfx_tab[i + 1].loop = (uint16_t)((t[4] << 8) | t[5]);
        sfx_tab[i + 1].len = (uint16_t)((t[6] << 8) | t[7]);
    }
    pcm_onoff = 0xFF;
    write_byte(PCM_ONOFF, 0xFF);                               /* all channels off */
    for (b = 0; b < 16; b++) {                                 /* 16 banks x 4 KB */
        write_byte(PCM_CTRL, (uint8_t)b);                      /* sound off, wave bank b */
        for (i = 0; i < 4096; i++) {
            uint32_t a = b * 4096 + i;
            write_byte(PCM_WAVE + 2 * i, a < img ? buf[hdr + a] : 0xFF);   /* unused = stop */
        }
    }
    write_byte(PCM_CTRL, 0x80);                                /* sound on */
    return sfx_count;
}


/* =====================================================================================
 * K36 BACKUP RAM (Sega CD internal) - high scores + options, one 128-byte file.
 *   SONIC REV R4C (MicroNut99's option 2): the file is Sonic CD's own - SONICCD32X_, 512 bytes (8 blocks): the
 *   engine packs the game's save files, options and time attack records into its 256 words
 *   (cd32x/engine/plat_mars.cpp, plat_save_*); CMD 57 / 58 reach all 256 words.
 *   BIOS entry _BURAM = 0x5F16: d0 = function, a0/a1 = parameters, carry set = error.
 *   BRMINIT 0 (a0 = 0x640-byte work area, a1 = 12-byte string buffer), BRMREAD 3 (a0 = 11-char
 *   file name, a1 = buffer), BRMWRITE 4 (a0 = name(11) + mode(1: 0 = normal) + blocks(word,
 *   64 bytes each), a1 = data).  All registers are saved around the call.
 *   The SH2 exchanges the 128 bytes one word at a time over COMM (CMD 57 get / 58 put) - no
 *   frame buffer, no Word RAM, only at start-up and after a game.
 * ===================================================================================== */
#define BRAM_BYTES 512                   /* SONIC REV R4C: 128 (Kobo's) -> 512 */
static uint8_t bram_work[0x640];
static uint8_t bram_str[16];
static uint8_t bram_buf[BRAM_BYTES];
static const char bram_name[12] = "SONICCD32X_";   /* SONIC REV R4C: Sonic CD's own file (was KOBODELUXE_) */
static uint8_t bram_param[14];
static int bram_ready = 0;

static int bram_call(int fn, const void *pa0, void *pa1) {
    register int d0 __asm__("d0") = fn;
    register const void *a0 __asm__("a0") = pa0;
    register void *a1 __asm__("a1") = pa1;
    register int d1 __asm__("d1") = 0;
    __asm__ volatile (
        "movem.l %%d2-%%d7/%%a2-%%a6,-(%%sp)\n\t"
        "jsr 0x5F16.w\n\t"
        "scs %%d1\n\t"                          /* carry (error) -> d1 low byte 0xFF */
        "movem.l (%%sp)+,%%d2-%%d7/%%a2-%%a6"
        : "+d"(d0), "+a"(a0), "+a"(a1), "+d"(d1) : : "cc", "memory");
    return (d1 & 0xFF) ? -1 : d0;
}
static int bram_init(void) {
    if (!bram_ready) bram_ready = bram_call(0, bram_work, bram_str) >= 0 ? 1 : -1;
    return bram_ready > 0;
}
static int bram_load(void) {                    /* 1 = loaded, 0 = no file / no BRAM */
    int i;
    for (i = 0; i < BRAM_BYTES; i++) bram_buf[i] = 0;
    if (!bram_init()) return 0;
    return bram_call(3, bram_name, bram_buf) >= 0 ? 1 : 0;
}
static int bram_save(void) {
    int i;
    if (!bram_init()) return 0;
    for (i = 0; i < 11; i++) bram_param[i] = (uint8_t)bram_name[i];
    bram_param[11] = 0;                         /* normal (not protected) mode */
    bram_param[12] = 0; bram_param[13] = BRAM_BYTES / 64;   /* blocks */
    return bram_call(4, bram_param, bram_buf) >= 0 ? 1 : 0;
}

static int cdda_start(int track, int *st_out)
{
    int st = 0;
    bios_wait_ready(0);
    scd_bios_call(0x0089, 0, 0);            
    scd_bios_call(0x0002, 0, 0);            
    scd_bios_call(0x0085, 0x0400, 0);       
    cdda_track_word = (uint16_t)track;
    scd_bios_call(0x0013, 0, &cdda_track_word);  
    st = scd_cd_status();
    *st_out = st; 
    return 1;
}

int main(void)
{
    write_byte(0x8003, read_byte(0x8003) & ~1);
    while (read_byte(0x8003) & 2);
#if !KOBO_BOOT_TEXT
    do_md_cmd1(MD_CMD_SET_VDP_REG, 0x8134);   /* K30: Genesis display off (md_init_hw left it on) */
#endif

    char *temp = (char *)0x0C0000;
    char tbuf[64];
    uint32_t build_counter = 0;
    int cart_ok = 0;

#ifndef SCD_BUILD_TEXT
#define SCD_BUILD_TEXT "BUILD: SONIC CD ?"
#endif
    print_diag(temp, SCD_BUILD_TEXT "      ", 2, 0); /* S7b2: names the round and the disc (Makefile) */
    print_diag(temp, "LOADING 32XCD ENGINE...           ", 2, 2);

    {
        const char *m = find_sh2_marker(sh2_app_start, sh2_app_length);
        if (m) sprintf(tbuf, "32X PAYLOAD: %d BYTES SH2-V%c%c        ", sh2_app_length, m[0], m[1]);
        else   sprintf(tbuf, "32X PAYLOAD: %d BYTES SH2:NONE       ", sh2_app_length);
        print_diag(temp, tbuf, 2, 4);
    }

    memcpy(temp, sh2_app_start, sh2_app_length);
    switch_banks();
    {   
        int r32 = do_md_cmd2(MD_CMD_INIT_32X, 0x200000, sh2_app_length);
        do_md_cmd0(MD_CMD_INSTALL_EXC);            /* HW1: hook the 68K error vectors (after the 32X is on) */
        g_nexc = 0;
        char a[12], b[12], c[12];
        fourcc(a, read_long(0xFF8014));   
        fourcc(b, read_long(0xFF8018));   
        fourcc(c, read_long(0xFF801C));   
        switch_banks();
        
        sprintf(tbuf, "INIT32X:%d 1ST:%s              ", r32, a);
        print_diag(temp, tbuf, 2, 19);
        sprintf(tbuf, "END C0:%s C4:%s          ", b, c);
        print_diag(temp, tbuf, 2, 20);
        if (r32 < 0) {
            print_diag(temp, "32X INIT FAILED - STOPPED       ", 2, 21);
            while (1) bump_fm();
        }

        do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15124, 0x1234);

        {
            uint16_t ready = 0;
            volatile int wait = 0;
            
            {
                unsigned int t0 = GET_TICKS;
                for (wait = 0; wait < 1000000; wait++) {
                    ready = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512A);
                    if (ready == 0x0AAA) break;
                    if ((GET_TICKS - t0) >= 180) break;
                }
            }
            sprintf(tbuf, "SH2 READY FOR ISOLATED TEST: %04X    ", ready);
            print_diag(temp, tbuf, 2, 19);

            if (ready != 0x0AAA) {
                print_diag(temp, "SH2 NEVER SIGNALLED READY - STOPPED", 2, 20);
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512A, 0x0BBB);   
                while (1) bump_fm();
            }
        }

#if KOBO_NOCART
        cart_ok = 0;                                   /* no-cart build: never touch the cart */
        print_diag(temp, "CART: NOT USED (NO-CART BUILD)     ", 2, 25);
#else
        cart_ok = safe_cart_present(temp, tbuf);  
#endif
        (void)cart_ok; 

        do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512A, 0x0BBB);   
    }

    uint16_t init_err = InitCD();

    {
        unsigned char c = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "POST-INITCD COMM: %02X          ", c);
        print_diag(temp, tbuf, 2, 1);
    }

    if (init_err != 0) {
    } else {
        print_diag(temp, "CD DRIVE READY                ", 2, 5);
        print_diag(temp, "BOOTING SUB-CPU...            ", 2, 6);
    }

    {   
        int st = scd_cd_status();
        int t2 = scd_track_info(2);
        sprintf(tbuf, "CD TRACKS %02X-%02X  TRACK2 %08X      ", (st >> 8) & 0xFF, st & 0xFF, t2);
        print_diag(temp, tbuf, 2, 14);
    }

    memcpy(temp, Sub_Start, Sub_End - Sub_Start);

    {
        unsigned char c = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "POST-MEMCPY2 COMM: %02X         ", c);
        print_diag(temp, tbuf, 2, 3);
    }

    print_diag(temp, "INITIALIZING SUB-CPU Sfx...   ", 2, 7);
    send_md_cmd0('I'); 

    {
        unsigned char c = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "POST-SENDMD COMM: %02X          ", c);
        print_diag(temp, tbuf, 2, 18);
    }

    print_diag(temp, "68K LISTENER ACTIVE           ", 2, 8);

    {
        unsigned char boot_comm = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "BOOT COMM PORT: %02X            ", boot_comm);
        print_diag(temp, tbuf, 2, 9);
    }

    print_diag(temp, "(TEXT BLINKS TO SHOW SH2 IS ALIVE)", 2, 26);

#ifndef SCD_CART_TEST
#define SCD_CART_TEST 0
#endif
#if !SCD_CART_TEST // S6b: the cart test disc keeps Kobo's command loop serving the SH2 (CMD 38 / 46)
    /* ===== SONIC CD S3B4: the plane demo hand-over. Everything above is Kobo's main.c as is. =====
       MDPLANE.BIN -> the Sub-CPU's Word RAM bank -> switch_banks() -> the vertical blank jump at
       0xFFFD08 set to 0x200000 (see below). print_diag() writes its text at the start of the same
       bank, so the file's first piece is read again after printing. */
    {
        int64_t fi = scd_open_file("MDPLANE.BIN");
        uint32_t len, lba, nsec, piece, first, done = 0;
        uint16_t w;
        if (fi < 0) {
            print_diag(temp, "SCD: MDPLANE.BIN NOT FOUND - STOPPED   ", 2, 22);
            while (1) bump_fm();
        }
        len   = (uint32_t)(fi >> 32);
        lba   = (uint32_t)(fi & 0xFFFFFFFF);
        nsec  = (len + 2047) / 2048;
        sprintf(tbuf, "SCD: MDPLANE.BIN %lu BYTES %lu SECT  ", (unsigned long)len, (unsigned long)nsec);
        print_diag(temp, tbuf, 2, 22);
        if (nsec > 64) {
            print_diag(temp, "SCD: MDPLANE.BIN OVER 128 KB - STOPPED ", 2, 23);
            while (1) bump_fm();
        }
        /* scd_open_file() prints its CKPT1 line with ONE switch_banks() (scd.c scd_diag_print),
           so the banks have been swapped an odd number of times. The listener's 'M' waits for
           RET = 1 (bit 0 of 0xA12003) before it jumps: the last switch_banks() below must leave
           RET = 1, so RET must be 0 while the file is loaded into our bank. */
        if (read_byte(0x8003) & 1)
            switch_banks();
        piece = (nsec + 7) / 8;
        first = piece < nsec ? piece : nsec;
        while (done < nsec) {
            uint32_t n = nsec - done < piece ? nsec - done : piece;
            scd_sub_read((void *)(temp + done * 2048), (int)(lba + done), (int)n);
            done += n;
            sprintf(tbuf, "SCD: READ %lu/%lu SECTORS          ", (unsigned long)done, (unsigned long)nsec);
            print_diag(temp, tbuf, 2, 23);
        }
        scd_sub_read((void *)temp, (int)lba, (int)first);
        w = read_word(0x0C0000);
        sprintf(tbuf, "SCD: FILE START %04X (46FC=OK) RET %d ", w, read_byte(0x8003) & 1);
        print_diag(temp, tbuf, 2, 24);
        if (w != 0x46FC)
            while (1) bump_fm();
        if (read_byte(0x8003) & 1) { /* the last switch would leave RET = 0: 'M' would never jump */
            print_diag(temp, "SCD: RET 1 BEFORE SWITCH - STOPPED   ", 2, 27);
            while (1) bump_fm();
        }
        print_diag(temp, "SCD: VBLANK -> 200000 (GENESIS PROGRAM)", 2, 27);
        scd_sub_read((void *)temp, (int)lba, (int)first); /* undo the text */
        switch_banks(); /* the Genesis sees MDPLANE.BIN at 0x200000 (as INIT_32X's hand-over) */
        /* The listener's 'M' never jumped (its RET test does not pass here). Instead: the vertical
           blank jump address at 0xFFFD08 (Kobo's listener installs its own handler there) is set
           to 0x200000 with the listener's write command (one move.l: an interrupt cannot split
           it). At the next vertical blank the 68000 enters the Genesis program, which masks
           interrupts and installs its own handler first. No 68K commands after this. */
        do_md_cmd2(MD_CMD_WRITE_32X_32, 0xFFFD08, 0x200000);
        while (1) bump_fm();
    }
#endif

    while (1) {
        build_counter++;

        if (build_counter == 1) {
            unsigned char first_iter_comm = read_byte(SUB_MAIN_FLAG);
            sprintf(tbuf, "ITER1 COMM PORT: %02X           ", first_iter_comm);
            print_diag(temp, tbuf, 2, 15);
        }

        bump_fm();

#if KOBO_HW_QUIET
        if (sub_quiet) {                 /* HW9 QUIET TEST: no 68K traffic except this heartbeat */
            static unsigned int t_last = 0;
            static uint16_t q_count = 0;
            unsigned int now = GET_TICKS;             /* read directly - no 68K command */
            if (now - t_last >= 30) {
                t_last = now;
                q_count++;
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512E,
                           ((read_byte(0xFF8038) & 0x0F) << 12) | (q_count & 0x0FFF));
            }
            continue;
        }
#endif
        {   /* HW1 SUB-CPU HEARTBEAT (K29): every 16 passes the pass counter goes to COMM14.  Only
               the Sub-CPU writes COMM14; the SH2 only reads it (hardware readout "P"). */
            static uint16_t hb_pass = 0;
            /* HW8: top digit = the CD drive's status (CDD status register 0xFF8038, first
               nibble: 0 stopped, 1 playing, 2 seeking, 3 scanning, 4 paused, 5 tray open,
               9 reading TOC, 0xB no disc / error ...), lower 3 digits = pass count / 64 */
            if ((++hb_pass & 63) == 0)
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512E,
                           ((read_byte(0xFF8038) & 0x0F) << 12) | ((hb_pass >> 6) & 0x0FFF));
        }
        floor_step();                    /* SONIC REV R5: the floor's work, a piece a pass */
        uint16_t cmd = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15120);
        if (cmd == 0) {                  /* HW6: idle - pause ~0.2 ms before the 68K reads COMM0
                                            again (fewer reads while the SH2 may be writing it) */
            volatile int k;
            for (k = 0; k < 150; k++) { }
        }

        /* KOBO K5 - PAD FAST PATH.  WHY: the SH2 asks for the pad (CMD 48) every frame and
           only waits a short time.  The rest of this loop (3 more register reads + redrawing
           row 10, each a round trip through the 68K) took longer than that, so the SH2 gave
           up and scrolled with pad = 0.  Answer CMD 48 first and start the next pass. */
        if ((cmd & 0xFF) == 48) {
            uint16_t pad = GET_PAD(0);
            if (cmd >> 8) pcm_play(cmd >> 8);           /* K33: sound effect requested */
            /* K32: reply = pad (COMM8, upper 16 bits) + the 68K's vblank counter (COMM10, lower
               16 bits) in ONE 32-bit write.  The SH2 uses the counter to run Kobo's logic at
               its fixed 30 ms step, whatever the frame rate.  COMM10 is only used by the
               start-up handshake, long finished by now. */
            do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, ((uint32_t)pad << 16) | (GET_TICKS & 0xFFFF));
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);     /* done           */
            continue;
        }

        uint16_t comm4 = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15124);
        uint16_t sh2_state = do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512C);

        {   /* HW5: redraw row 10 ONLY when the SH2's step code changes, and never again once the
               game runs (0x4B10).  WHY: the old test also watched COMM0 (flips 00/30 every
               frame with the pad request) and COMM8/10 (pad + frame counter since K32), so on
               hardware (text on) the Sub-CPU reprinted a text row EVERY pass - each print swaps
               Word RAM and goes through the 68K - and the pad replies waited: 1-2 fps. */
            static uint16_t l_st = 0xFFFF;
            static int running = 0;
            if (!running && sh2_state != l_st) {
                uint32_t ckpt = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
                l_st = sh2_state;
                sprintf(tbuf, "32X:%X 32XReg:%X STATE:%X CKPT:%04X     ", cmd, comm4, sh2_state, (unsigned int)(ckpt >> 16));
                print_diag(temp, tbuf, 2, 10);
                if (sh2_state == 0x4B10) { running = 1; sub_quiet = KOBO_HW_QUIET; }   /* HW9 */
            }
        }

        if (planes_on) {
            /* SONIC REV R1: COMM4 carries the plane record ids now - no start-up checks on it */
        } else if (comm4 == 0x0EEE) {
            print_diag(temp, "SH2 REACHED THE LDC LINE              ", 2, 18); 
        } else if (comm4 != 0x1234 && comm4 != 0x0000) {
            print_diag(temp, "SH2 NEVER EVEN REACHED THIS FILE      ", 2, 18); 
        }

        /* SONIC REV R5: a command that uses Word RAM (loads, sound banks, backup RAM ...) ends the floor first */
        if (floor_on && cmd != 0 && cmd != FLOOR_CMD && cmd != 48 && cmd != 52 && cmd != 45 && cmd != 54 &&
            cmd != 57 && cmd != 58)
            floor_end();

        if (cmd == FLOOR_CMD) {
            int arg = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int op = arg & 0xFF, hi = (arg >> 8) & 0xFF;
            if (op == 0) {
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, floor_begin(hi));
            }
            else if (op >= 1 && op <= 4) {
                int32_t v = (int32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
                if (op == 1) flr_x = v;
                else if (op == 2) flr_z = v;
                else if (op == 3) flr_y = v;
                else {
                    flr_sin = (int16_t)(v >> 16);
                    flr_cos = (int16_t)v;
                    if (floor_on) {
                        if (floor_busy) floor_pend = 1;
                        else floor_start();
                    }
                }
            }
            else if (op == 5 && floor_on && hi >= 1 && hi <= 15) {
                uint32_t v = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xC00004, 0xC0000000 | ((uint32_t)(0x60 + 2 * hi) << 16));
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xC00000, (int)(v >> 16));
                if (hi < 15)
                    do_md_cmd2(MD_CMD_WRITE_32X_16, 0xC00000, (int)(v & 0xFFFF));
            }
            else if (op == 6) {
                /* the file's palette index of colour hi: it sits in Word RAM (0x08 + 2 hi - 2) - only the Sub-CPU's
                   while no frame is on its way */
                int v = 0;
                if (floor_on && hi >= 1 && hi <= 15 && floor_busy == 0)
                    v = read_word(FLR_SUB + 8 + 2 * (hi - 1));
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, v);
            }
            else if (op == 7) {
                floor_end();
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 38) { 
            int img_idx = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            char img_name[24];
            cur_img = img_idx < 0 ? 0 : img_idx;         
            /* KOBO M1: file number 16 = the Kobo graphics pack (tools/kobo_gfx.py).
               CMD 46 then stores it at cart offset 16 * CART_SLOT = 2 MB. */
            if (img_idx == 16) { strcpy(img_name, KOBO_PACK_NAME); cur_img = KOBO_PACK_SLOT; }
            else if (img_idx == 21) { strcpy(img_name, "SONICCD.PAK"); cur_img = 0; }  /* S7c: the engine at cart 0 (as OpenLara's file 20) */
            else if (img_idx == 22) { strcpy(img_name, "SCDDATA.PAK"); cur_img = 24; } /* S7e: the game's data pack at cart 0x300000 */
            else if (img_idx >= 0x100 && img_idx < 0x500) {                              /* S7d: a scene's cart image at cart 0x080000 */
                sprintf(img_name, "L%dS%03d.IMG", (img_idx >> 8) - 1, img_idx & 0xFF); cur_img = 4; }
            else if (img_idx >= 0x1100 && img_idx < 0x1500) {                            /* SONIC REV R1: the act's Genesis data at cart 0x380000 */
                sprintf(img_name, "G%dS%03d.MDG", ((img_idx >> 8) & 0xF) - 1, img_idx & 0xFF); cur_img = SGB_SLOT; }
            else if (img_idx <= 0) strcpy(img_name, "IMAGE.RAW");
            else sprintf(img_name, "IMAGE%d.RAW", img_idx);

            sprintf(tbuf, "OPENING %s...              ", img_name);
            print_diag(temp, tbuf, 2, 11);

            unsigned char entry_comm = read_byte(SUB_MAIN_FLAG);
            if (entry_comm != 0) {
                sprintf(tbuf, "ENTRY COMM FAULT: %02X        ", entry_comm);
                print_diag(temp, tbuf, 2, 12);
            }

            int64_t file_info = scd_open_file(img_name);
            if (file_info < 0) {
                sprintf(tbuf, "ERR: %s NOT FOUND          ", img_name);
                print_diag(temp, tbuf, 2, 12);
                sprintf(tbuf, "TIMEOUT: %d (0=none 1=cmd 2=ack)", scd_last_timeout_code);
                print_diag(temp, tbuf, 2, 13);
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, 0); 
                saved_len = 0;
            } else {
                uint32_t length = (uint32_t)(file_info >> 32);
                saved_lba = (uint32_t)(file_info & 0xFFFFFFFF);
                saved_len = length;                                   /* K10: for CMD 50 */
                sprintf(tbuf, "%s %lu BYTES%s          ", img_name, (unsigned long)length,
                        length > 128000 ? " (TOO TALL)" : " OK");
                print_diag(temp, tbuf, 2, 12);
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, length); 
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0); 
        }
        else if (cmd == CMD_CHUNK_TO_CART && (do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xF800) == 0xF800) {
            /* SONIC REV R4E - THE PAGE SWITCH of MicroNut99's 8 / 16 MB cart: two 2 MB windows (window 0 = cart
               0x000000-0x1FFFFF, window 1 = 0x200000-0x3FFFFF), each showing one of 8 pages of 2 MB. COMM2 = 0xF800 |
               window << 8 | page (a fast run's COMM2 is at most 0x03FF). The page goes to 0xA130F0 (window 0) or
               0xA130F2 (window 1) in D8-D10: a byte write to the even address - the 68000's SAFE_CART_XFER (command 26:
               interrupts off, RV = 1, as the cart needs; nothing new in hw_md.s). The SH2 purges its cache after it. A
               cart without the mapper ignores the write (the 4 MB game never sends this). COMM12 = 0. */
            int arg = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            PB(0) = (uint8_t)(arg & 7);
            switch_banks();
            cx(XM(0), (arg & 0x100) ? 0xA130F2 : 0xA130F0, 1);
            switch_banks();
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, 0);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == CMD_CHUNK_TO_CART && (do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFFE0) == 0xF000) {
            /* SONIC REV R4F - THE FILE'S CART SLOT: COMM2 = 0xF000 | slot (0-31, 128 KB each) - the file command 38
               opened goes to that slot instead of its own (the 16 MB cart: the engine's copy and the data pack into a
               page shown in window 1, cart 0x200000-0x3FFFFF). Only the 16 MB game sends it. COMM12 = 0. */
            cur_img = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0x1F;
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, 0);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == CMD_CHUNK_TO_CART && (do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFF00)) {
            /* SONIC REV R4C - FAST LOAD: COMM2 = first chunk | (more chunks) << 8 - up to CHUNK_RUN_MAX chunks of the
               open file in ONE CD read (one seek instead of one a chunk) into this side's Word RAM bank (128 KB), then
               into the cart as below, without the read-back: the reply is a checksum of what was read (COMM8, 32-bit,
               load_sum) and the SH2 checks the cart against it - a mismatch, and it loads those chunks again one at a
               time the old way (COMM2 high byte 0: the verified path below, unchanged). COMM12 = 0 done, 1 nothing read. */
            int arg = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int length = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
            int chunk = arg & 0xFF, count = ((arg >> 8) & 0xFF) + 1;
            int off, n = 0, bytes, p;
            uint32_t coff, sum = 0;
            if (count > CHUNK_RUN_MAX) count = CHUNK_RUN_MAX;
            off = chunk * 32768;
            coff = CART_IMG_BASE + (uint32_t)cur_img * CART_SLOT + (uint32_t)off;
            if (off < length) n = (length - off + 2047) / 2048;
            if (n > 16 * count) n = 16 * count;
            bytes = n * 2048;
            if (n > 0) {
                scd_sub_read((void *)0x0C0000, saved_lba + chunk * 16, n);
                sum = load_sum((const uint32_t *)0x0C0000, (uint32_t)bytes >> 2);
                switch_banks();
                for (p = 0; p < bytes; p += 0x4000)
                    do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x400000 + coff + p, 0x200000 + p,
                               bytes - p < 0x4000 ? bytes - p : 0x4000);
                switch_banks();
            }
            sprintf(tbuf, "CART I%d C%d-%d @%06lX %s          ", cur_img, chunk, chunk + count - 1,
                    (unsigned long)(0x400000 + coff), n == 0 ? "EMPTY" : "FAST");
            print_diag(temp, tbuf, 2, 13);
            do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, (int)sum);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, n > 0 ? 0 : 1);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == CMD_CHUNK_TO_CART) {
            int chunk = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int length = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
            int off = chunk * 32768;
            int n = 0, bytes, p, k, bad = 0;
            uint32_t coff = CART_IMG_BASE + (uint32_t)cur_img * CART_SLOT + (uint32_t)off;

            if (off < length) n = (length - off + 2047) / 2048;
            if (n > 16) n = 16;
            bytes = n * 2048;

#if CART_TARGET == 1
            if (n > 0) {
                volatile uint8_t *flag = (volatile uint8_t *)0x0DF000;   
                scd_sub_read((void *)0x0C0000, saved_lba + chunk * 16, n);
                flag[0] = 1; flag[1] = 0;
                switch_banks();                                   
                cx(0x21F000, 0x7FFFFF, 1);                        
                for (p = 0; p < bytes; p += 4096)
                    do_md_cmd3(MD_CMD_BRAM_PUT, 0x200000 + p, 0x600001 + 2 * (coff + p),
                               bytes - p < 4096 ? bytes - p : 4096);
                cx(0x21F001, 0x7FFFFF, 1);                        
                for (p = 0; p < bytes; p += 4096)                 
                    do_md_cmd3(MD_CMD_BRAM_GET, 0x600001 + 2 * (coff + p), 0x208000 + p,
                               bytes - p < 4096 ? bytes - p : 4096);
                switch_banks();                                   
                {
                    volatile uint8_t *disc = (volatile uint8_t *)0x0C0000;
                    volatile uint8_t *cart = (volatile uint8_t *)0x0C8000;
                    for (k = 0; k < bytes; k++) {
                        if (disc[k] != cart[k]) {
                            sprintf(tbuf, "MISMATCH C%d +%05X DISC %02X CART %02X ", chunk, k,
                                    disc[k], cart[k]);
                            print_diag(temp, tbuf, 2, 17);
                            bad = 1;
                            break;
                        }
                    }
                }
            }
#else
            if (n > 0) {
                scd_sub_read((void *)0x0C0000, saved_lba + chunk * 16, n);   
                switch_banks();                                   
                for (p = 0; p < bytes; p += 0x4000)
                    do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x400000 + coff + p, 0x200000 + p,
                               bytes - p < 0x4000 ? bytes - p : 0x4000);
#if CART_VERIFY_READBACK
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(coff >> 20) & 3);   
                for (p = 0; p < bytes; p += 0x4000)
                    do_md_cmd3(MD_CMD_CHILLY_MEMCPY, 0x208000 + p,
                               0x900000 + ((coff + p) & 0xFFFFF),
                               bytes - p < 0x4000 ? bytes - p : 0x4000);
#endif
                switch_banks();                                   
#if CART_VERIFY_READBACK
                {   
                    volatile uint8_t *disc = (volatile uint8_t *)0x0C0000;
                    volatile uint8_t *cart = (volatile uint8_t *)0x0C8000;
                    for (k = 0; k < bytes; k++) {
                        if (disc[k] != cart[k]) {
                            sprintf(tbuf, "MISMATCH C%d +%05X DISC %02X CART %02X ", chunk, k,
                                    disc[k], cart[k]);
                            print_diag(temp, tbuf, 2, 17);
                            bad = 1;
                            break;
                        }
                    }
                }
#endif
#if CART_WINDOW_READS
                {   
                    int wbad = 0;
                    uint16_t want = 0, got = 0;
                    uint32_t o = 0;
                    do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(coff >> 20) & 3);   
                    for (k = 0; k < 4 && !wbad; k++) {
                        o = (uint32_t)(((bytes - 2) / 3 * k) & ~1);
                        want = *(volatile uint16_t *)(0x0C0000 + o);
                        got  = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0x900000 + ((coff + o) & 0xFFFFF));
                        if (got != want) wbad = 1;
                    }
                    if (wbad)
                        sprintf(tbuf, "32X WIN C%d +%05lX W%04X R%04X   ", chunk, (unsigned long)o, want, got);
                    else
                        sprintf(tbuf, "32X WIN C%d 900000: OK            ", chunk);
                    print_diag(temp, tbuf, 2, 16);
                }
#else
                print_diag(temp, "32X WIN: OFF (V72 TEST)          ", 2, 16);
#endif
            }
#endif
            sprintf(tbuf, "CART I%d C%d @%06lX %s          ", cur_img, chunk,
#if CART_TARGET == 1
                    (unsigned long)(0x600001 + 2 * coff),      
#else
                    (unsigned long)(0x400000 + coff),          
#endif
                    n == 0 ? "EMPTY" : bad ? "BAD" : (CART_VERIFY_READBACK ? "OK" : "NOVFY"));
            print_diag(temp, tbuf, 2, 13);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, bad);   /* HW4: 16-bit COMM12 - COMM14 is the heartbeat */
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == CMD_CART_TO_FB) {
            int slot_chunk = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int slot = slot_chunk >> 8;
            int chunk = slot_chunk & 0xFF;
            int length = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
            int off = chunk * 32768, bytes = length - off, p;
            uint32_t coff = CART_IMG_BASE + (uint32_t)slot * CART_SLOT + (uint32_t)off;
            
            if (bytes > 32768) bytes = 32768;
            for (p = 0; p < bytes; p += 4096)
                do_md_cmd3(MD_CMD_BRAM_TO_FB, 0x600001 + 2 * (coff + p), 0x840200 + off + p,
                           bytes - p < 4096 ? bytes - p : 4096);
            
            sprintf(tbuf, "CART->FB S%d C%d %d BYTES          ", slot, chunk, bytes > 0 ? bytes : 0);
            print_diag(temp, tbuf, 2, 16);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 44) {
#if !SLIDESHOW_DEBUG
            if (!quiet) { do_md_cmd0(MD_CMD_CLEAR_B); do_md_cmd0(MD_CMD_CLEAR_A); quiet = 1; }
#endif
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 50 || cmd == 51) {
            /* K15: CMD 51 = same as CMD 50, but chunk c goes to frame buffer byte
               0x200 + c * 32 KB (a whole picture in order - the splash screen). */
            /* KOBO NO-CART - CMD 50 "file chunk -> frame buffer (raw)".
               COMM2 = chunk number (32 KB each).  File length: saved_len from CMD 38 (K10 -
               no longer read from COMM8).  CD -> Word RAM (Sub-CPU) -> frame buffer at 68K 0x840200 via MD
               command 25 copy_words with flag 0 = plain copy (flag 1 would set bit 15 on every
               word, which is only right for 15-bit pictures).  The SH2 has handed the frame
               buffer to the 68K (FM=0) before sending this. */
            int chunk = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int length = (int)saved_len;           /* K10: from CMD 38, NOT from COMM8 */
            /* K31: CMD 50 chunks are 16 KB (8 sectors) - the off-screen loading window shrank
               to 16 KB so all 5 tile sets fit in spare frame-buffer memory (no CD reads after
               the music starts: a data read silences the CD audio).  CMD 51 stays 32 KB. */
            int sec = cmd == 51 ? 16 : 8;                   /* sectors per chunk */
            int off = chunk * sec * 2048;
            int n = 0;
            if (off < length) n = (length - off + 2047) / 2048;
            if (n > sec) n = sec;
            uint16_t w0 = 0;
            /* (K13 cleared Word RAM priority mode here; K13 showed it was never on - register 05
               before and after - so the write is removed in K15: no needless pokes at the Word
               RAM mode register on real hardware.  The real K12 fault was zero words not landing
               in the frame buffer - fixed on the SH2 side by clearing the area first.) */
            if (n > 0) {
                scd_sub_read((void *)0x0C0000, saved_lba + chunk * sec, n);  /* CD -> Word RAM */
                switch_banks();                                               /* -> 68K side    */
                do_md_cmd4(MD_CMD_COPY_WORDS, 0x200000,
                           cmd == 51 ? 0x840200 + off : 0x852000, n * 2048, 0);
                /* K30: CMD 50 now lands OFF-SCREEN (frame buffer byte 0x12000, past the
                   visible 320x224 picture which ends at 0x11A00): loading never overwrites
                   what is on screen (the panel stays intact).  SH2 side: FB_CHUNK_BUF. */
                switch_banks();                                               /* -> Sub side    */
                w0 = *(volatile uint16_t *)0x0C0000;   /* first word read from CD (read it
                                                          BEFORE print_diag reuses this buffer) */
            }
            /* K11 reply for the SH2's diagnostic strip (lines 1-3): chunk | sectors | length>>8.
               Written before COMM0 is cleared; the SH2 reads it only after that. */
            /* K14 reply: chunk | sectors | the word in WORD RAM at offset 0x210 of this chunk, read
               after the copy (the Sub-CPU's buffer at 0x0C0000).  For chunk 0 that is the high
               word of the tile set offset and must be 0000.  Correct here but wrong in the
               frame buffer (strip line T) = the 68K -> frame buffer copy loses zero data.
               (K13: priority mode was never on - register read 05 before and after.)        */
            {
                uint16_t wr = (n > 0) ? *(volatile uint16_t *)(0x0C0000 + 0x210) : 0xEEEE;
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA1512C, (chunk << 24) | (n << 16) | wr);
            }
            (void)w0;
            sprintf(tbuf, "CD->FB CHUNK %d  %d SECTORS          ", chunk, n);
            print_diag(temp, tbuf, 2, 16);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 54) {
            /* K36 - CMD 54 "music off": BIOS MSCSTOP (the same call cdda_start makes first).
               Music on again = CMD 45 as always. */
            bios_wait_ready(0);
            scd_bios_call(0x0002, 0, 0);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 56 || cmd == 59) {
            /* K36 - CMD 56 load / CMD 59 save the backup-RAM file.  Reply COMM12: 1 ok, 0 not. */
            int ok = cmd == 56 ? bram_load() : bram_save();
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, ok);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 57 || cmd == 58) {
            /* K36 - CMD 57 get / CMD 58 put one word of the buffer: COMM2 = word index,
               COMM8 = value (58: written by the SH2 before the command; 57: our reply).
               SONIC REV R4C: 256 words (BRAM_BYTES 512) */
            int wi = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & (BRAM_BYTES / 2 - 1);
            if (cmd == 58) {
                int v = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15128);
                bram_buf[wi * 2] = (uint8_t)(v >> 8); bram_buf[wi * 2 + 1] = (uint8_t)v;
            } else {
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15128, (bram_buf[wi * 2] << 8) | bram_buf[wi * 2 + 1]);
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 53) {
            /* K33 - CMD 53 "load sound effects": KOBOSFX.BIN -> PCM chip.  Reply in COMM12:
               number of effects (0 = failed).  SONIC REV R4D: COMM2 = the bank (0 KOBOSFX.BIN, n SFX<n>.BIN) */
            int bank = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFF;
            int nfx = pcm_load(bank);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, nfx);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == SGB_CMD) {
            int op = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFF;
            uint32_t off = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
            int ok = 0;
            if (op == 1) {
                /* the file through the 32X bank window: 1 MB bank of the offset, 0x900000 + the rest.
                   The whole file must lie in that bank (the slot at 0x380000 does: 0x380000-0x3EFFFF). */
                uint32_t base = 0x900000 + (off & 0xFFFFF);
                do_md_cmd2(MD_CMD_WRITE_32X_16, SGB_FLAG_ADDR, 0);           /* records off while loading */
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(off >> 20) & 3);
                uint32_t magic = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, (int)base);
                uint32_t size  = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, (int)(base + 8));
                int ver        = do_md_cmd1(MD_CMD_READ_32X_16, (int)(base + 4));
                int cells      = do_md_cmd1(MD_CMD_READ_32X_16, (int)(base + 6));
                uint32_t oCell = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, (int)(base + 12));
                uint32_t oZero = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, (int)(base + 28));
                uint32_t oCram = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, (int)(base + 32));
                if (!(off & 1) && magic == 0x53434D47u && ver == 1 && cells > 0 && cells <= 1536 &&
                    (off & 0xFFFFF) + size <= 0x100000 && oCell < size && oZero < size && oCram < size) {
                    static const uint16_t regs[] = {
                        0x8134,                   /* display off, vertical blank interrupt stays on */
                        0x8004, 0x8230,           /* plane A 0xC000 */
                        0x8334, 0x8407,           /* window 0xD000 (unused), plane B 0xE000 */
                        0x857C, 0x8D3F,           /* sprites 0xF800, h-scroll 0xFC00 */
                        0x8700, 0x8AFF, 0x8B03,   /* backdrop colour 0, line h-scroll, full v-scroll */
                        0x8C81, 0x8E00, 0x8F02,   /* H40, no shadow / highlight, auto-increment 2 */
                        0x9001, 0x9100, 0x9200    /* planes 64 x 32, no window */
                    };
                    int i;
                    for (i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); i++)
                        do_md_cmd1(MD_CMD_SET_VDP_REG, regs[i]);
                    /* planes, sprite table and h-scroll table cleared (0xC000-0xFFFF from the zero block) */
                    do_md_cmd3(MD_CMD_COPY_VRAM_, 0xC000, (int)(base + oZero), 0x1000);
                    do_md_cmd3(MD_CMD_COPY_VRAM_, 0xE000, (int)(base + oZero), 0x1000);
                    do_md_cmd2(MD_CMD_WRITE_32X_32, 0xC00004, 0x40000010);   /* VSRAM 0 */
                    do_md_cmd2(MD_CMD_WRITE_32X_32, 0xC00000, 0);            /* plane A, plane B: 0 */
                    do_md_cmd3(MD_CMD_COPY_VRAM_, 0x0000, (int)(base + oCell), cells * 16);
                    do_md_cmd3(MD_CMD_SET_PALETTE_, (int)(base + oCram), 0, 64);
                    do_md_cmd2(MD_CMD_WRITE_32X_16, SGB_LAST_ADDR, 0);
                    do_md_cmd2(MD_CMD_WRITE_32X_16, SGB_READY_ADDR, 0);
                    planes_on = 1;                                            /* no more Kobo text */
                    do_md_cmd2(MD_CMD_WRITE_32X_16, SGB_FLAG_ADDR, 1);       /* heartbeat off ... */
                    do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15126, 0);            /* ... COMM6 = 0: no id yet */
                    do_md_cmd1(MD_CMD_SET_VDP_REG, 0x8174);                  /* display on */
                    ok = 1;
                }
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, 0);                 /* bank 0 again */
            }
            else if (op == 2) {
                do_md_cmd2(MD_CMD_WRITE_32X_16, SGB_FLAG_ADDR, 0);
                do_md_cmd0(MD_CMD_INIT_HW_);              /* the listener's VDP set-up, font, colours */
                do_md_cmd0(MD_CMD_CLEAR_B);
                do_md_cmd0(MD_CMD_CLEAR_A);
                planes_on = 0;
                ok = 1;
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, ok);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 52) {
            /* KOBO K19 - CMD 52 "Genesis text layer on/off": COMM2 = 1 on, 0 off.
               Switches the Genesis display itself (VDP register 1 bit 6) - off blanks ALL
               Genesis text (green diagnostics and Chilly's white messages) to black and the
               32X picture shows alone; on brings everything back (it stays in video RAM).
               WHY: the 32X priority bit alone did not hide the text reliably (K17/K18). */
            int on = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 1;
            do_md_cmd1(MD_CMD_SET_VDP_REG, on ? 0x8174 : 0x8134);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 48) {
            /* (K5: normally handled by the fast path at the top of the loop)
               KOBO M1 - CMD 48 "read pad": reply = pad 1 bits (SEGA_CTRL_*) in COMM8.
               WHY a command: the SH2 must never poll a comm register the 68K writes
               (32X manual p.69, and the COMM6 crash).  GET_PAD(0) is the copy the 68K's
               vblank handler keeps in its comm word - no extra 68K work. */
            uint16_t pad = GET_PAD(0);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15128, pad);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 45) {
            int track = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            sprintf(tbuf, "CDDA: PLAY TRACK %d (REPEAT)        ", track);
            print_diag(temp, tbuf, 2, 27);
            
            {
                int st = 0;
                int tries = cdda_start(track, &st);
                sprintf(tbuf, "CDDA T%d ST %02X TRY %d%s          ", track, (st >> 24) & 0xFF,
                        tries, tries ? "" : " FAILED");
                print_diag(temp, tbuf, 2, 27);
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
    }
    return 0;
}