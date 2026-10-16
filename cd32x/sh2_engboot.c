/* PORT32X S7d: the game start. Files (Sub-CPU mapping): 21 SONICCD.PAK = the engine (cart 0),
 * 22 SCDDATA.PAK = the game's data pack (cart 0x180000), 0x100 + list * 0x100 + scene = the scene's
 * cart image L<list>S<scene>.IMG (cart 0x080000). This loader puts the engine, the data pack and the
 * first scene (L0S000 = where the game starts) into the cart, then starts the engine. Its load
 * routine stays resident in SDRAM: the engine calls it at every scene change (the boot record holds
 * its address) - the engine itself runs from the cart and must not run while Kobo's CMD 46 copies
 * into the cart (RV set: the SH2 cannot read the cart then).
 * STATE: 5C01 engine, 5C02 data pack, 5C03 first scene loaded, 5C04 scene image OK, 5C05 jump;
 * 5Dnn chunk nn while loading; errors 5CE1 file missing (5CE1), 5CE2 chunk bad, 5CE3 file / image bad.
 * --- S7c notes: the SH2 boot loader for the engine - after the OpenLara CD32X loader
 * (sh2/cdloader_sh2.c) and Kobo's sh2_main.c main(), both proven on hardware:
 *   0. the hardware start-up EXACTLY as Kobo / OpenLara: COMM0 = 0, COMM10 checkpoints 0006 / 0007,
 *      interrupt mask, interrupts on, COMM10 0x0AAA -> wait 0x0BBB, take_fb
 *   1. the 32X layer stays BLANK (mode 0): Chilly's white lines and the green diagnostics are never
 *      covered (OpenLara). Progress = status codes in COMM12, shown by the Sub-CPU as STATE:xxxx
 *   2. [the splash: IMAGE.RAW, Kobo's show_splash() - its place in the sequence; OFF until the console
 *      boot is good (SCD_SPLASH 0)]
 *   3. the game: ONE file, SONICCD.PAK (file 21 -> cart offset 0; one mapping line in the Sub-CPU, as
 *      OpenLara's file 20), CMD 38 + CMD 46 per 32 KB chunk, every chunk verified by the Sub-CPU
 *   4. the stage image's CRCs checked in the cart (md/cart_verify.c)
 *   5. the boot record (0x06001F00: "SCDB", pak length, stage image offset, data pack length), cache
 *      purged, jump to the engine at cart 0x02000000 - interrupts stay on (as Kobo); the engine's
 *      clock = this program's vertical-blank interrupt (0x06001F10)
 * Status codes (COMM12, 16-bit - COMM14 is the Sub heartbeat):
 *   5C01 opening SONICCD.PAK   5Dnn chunk nn in the cart   5C02 pack loaded + verified
 *   5C04 stage image OK        5C05 jumping to the engine
 *   errors: 5CE1 SONICCD.PAK not on the CD   5CE2 bad chunk(s)   5CE3 pack / stage image bad
 *           5CE6 the engine returned         5CE7 this loader too big */
#include <stddef.h>
#include <stdint.h>
#include "32x.h"
#include "../md/cart_verify.h"

#ifndef SCD_SPLASH
#define SCD_SPLASH 0 /* IMAGE.RAW splash - OFF until the console boot is good */
#endif

const char sh2_build_marker[] = "SH2-VS7D"; /* the Sub-CPU looks for "SH2-V" */

#define SC_COMM2 (*(volatile uint16_t *)0x20004022)
#define SC_COMM8_32 (*(volatile uint32_t *)0x20004028)
#define SC_COMM10 (*(volatile uint16_t *)0x2000402A)
#define SC_COMM12 (*(volatile uint16_t *)0x2000402C)
#define SC_CMD_CLR (*(volatile uint16_t *)0x2000401A)
#define SC_SYS_ADAPTER_B (*(volatile uint8_t *)0x20004000)
#define SC_VDP_DISPMODE (*(volatile uint16_t *)0x20004100)
#define SC_CCR (*(volatile uint8_t *)0xFFFFFE92)
#define BOOT_RECORD ((volatile uint32_t *)0x06001F00)
#define VBLANK_COUNT (*(volatile uint32_t *)0x06001F10) /* the engine's clock */

#define CMD_OPEN_FILE 38
#define CMD_CHUNK_TO_CART 46
#define FILE_ENGINE 21         /* Sub-CPU: 21 -> "SONICCD.PAK" at cart 0 */
#define FILE_DATA 22           /* Sub-CPU: 22 -> "SCDDATA.PAK" at cart 0x180000 */
#define FILE_FIRST_SCENE 0x100 /* Sub-CPU: 0x100 -> "L0S000.IMG" at cart 0x080000 */
#define CHUNK_BYTES 32768
#define ENGINE_MAX 0x080000
#define SCENE_MAX 0x280000 /* S7e: 0x080000 - 0x300000 (sprite and tile pixels in the scene image) */
#define DATA_MAX 0x100000  /* S7e: 0x300000 - 0x400000 */
#define PAK_STAGE 0x080000 /* the scene image's place in the cart */
#define CART_U 0x22000000       /* the cart, cache-through */
#define ENGINE_ENTRY 0x02000000 /* the cart, cached: the engine's code */

/* S7e6: PC SAMPLING (suggested by the OpenLara / OpenJazz builders): once the engine runs, a stub in vector
   70 (V blank) saves the interrupted PC and chains to Kobo's pri_irq with every register unchanged; every 30
   vertical blanks this handler posts it in COMM12 - Kobo's line 10 shows where the SH2 is:
   8xxx = engine code at 0x02000000 + (xxx & 0x7FFF) * 16; 7xxx = SDRAM 0x06000000 + (xxx & 0xFFF) * 64.
   Line 10 frozen = the V-blank interrupt itself stopped. In SDRAM (this loader), never in the cart: during a
   scene load Kobo's CMD 46 sets RV and the SH2 cannot read the cart. */
volatile uint32_t scdLastPC = 0, scdOrigVBI = 0, scdSampling = 0;
void scd_vbi_sample(void);
__asm__(".text\n"
        "    .align 2\n"
        "    .global _scd_vbi_sample\n"
        "_scd_vbi_sample:\n"
        "    mov.l r0,@-r15\n"
        "    mov.l r1,@-r15\n"
        "    mov.l @(8,r15),r0\n"   /* the interrupted PC (the CPU pushed PC, then SR) */
        "    mov.l 1f,r1\n"
        "    mov.l r0,@r1\n"
        "    mov.l @r15+,r1\n"
        "    mov.l 2f,r0\n"
        "    mov.l @r0,r0\n"         /* Kobo's original V-blank vector (pri_irq) */
        "    jmp @r0\n"
        "    mov.l @r15+,r0\n"       /* delay slot: r0 restored; the jump target was read before */
        "    .align 2\n"
        "1:  .long _scdLastPC\n"
        "2:  .long _scdOrigVBI\n");
void pri_vbi_handler(void)
{
    static uint32_t n = 0;
    VBLANK_COUNT++;
    if (scdSampling && ++n >= 30) {
        uint32_t pc = scdLastPC;
        n           = 0;
        if (pc >= 0x02000000u && pc < 0x02080000u)
            SC_COMM12 = (uint16_t)(0x8000 | ((pc - 0x02000000u) >> 4));
        else if (pc >= 0x06000000u && pc < 0x06040000u)
            SC_COMM12 = (uint16_t)(0x7000 | ((pc - 0x06000000u) >> 6));
        else
            SC_COMM12 = 0x6FFF;
    }
}
void pri_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_dma1_handler(void) {}
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
extern char _bss_end[];
void *memset(void *d, int c, size_t n)
{
    volatile uint8_t *p = (volatile uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    volatile uint8_t *p       = (volatile uint8_t *)d;
    const volatile uint8_t *q = (const volatile uint8_t *)s;
    while (n--) *p++ = *q++;
    return d;
}
void secondary(void)
{
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    while (1) {
    }
}

static void show_status(uint16_t code) { SC_COMM12 = code; } /* Kobo / OpenLara */
static void comm_pause(void)                                 /* HW6: never hammer a COMM register */
{
    volatile int k;
    for (k = 0; k < 64; k++) {
    }
}
static void wait_cmd(uint16_t cmd) /* Kobo */
{
    uint32_t n;
    MARS_SYS_COMM0 = cmd;
    for (n = 0; n < 800000UL && MARS_SYS_COMM0 != 0; n++) comm_pause();
}
static void take_fb(void) /* Kobo */
{
    SC_SYS_ADAPTER_B = 0x80;
    while ((MARS_SYS_INTMSK & 0x8000) == 0) {
    }
}
static void halt(uint16_t code) /* OpenLara: stays on screen as STATE:5CEx */
{
    show_status(code);
    while (1) {
    }
}
/* the resident load routine (SDRAM): file `number` into its cart place - Kobo's load_pack loop.
   Returns the bad chunk count, -1 = not on the CD. Shows STATE:5Dnn per chunk. Called by this
   loader and by the engine at scene changes. */
int scd_load_file(uint16_t number, uint32_t *bytes) __attribute__((used, noinline));
int scd_load_file(uint16_t number, uint32_t *bytes)
{
    uint32_t len, nchunks, c;
    int bad = 0;
    SC_COMM2 = number;
    wait_cmd(CMD_OPEN_FILE);
    len = SC_COMM8_32;
    if ((int32_t)len <= 0)
        return -1;
    *bytes      = len;
    SC_COMM8_32 = len;
    nchunks     = (len + CHUNK_BYTES - 1) >> 15;
    for (c = 0; c < nchunks; c++) {
        SC_COMM2  = (uint16_t)c;
        SC_COMM12 = 0; /* the Sub-CPU replies 0 = chunk verified */
        wait_cmd(CMD_CHUNK_TO_CART);
        if (SC_COMM12 != 0)
            bad++;
        show_status((uint16_t)(0x5D00 | (c & 0xFF)));
    }
    return bad;
}

static uint16_t cart_read16(uint32_t offset) { return *(volatile uint16_t *)(CART_U + PAK_STAGE + offset); }

int main(void)
{
    CartCheck r;

    /* ---- 0. start-up: identical to Kobo / OpenLara ---- */
    MARS_SYS_COMM0 = 0;
    SC_COMM10      = 0x0006;
    MARS_SYS_INTMSK |= 0x0002;
    SC_COMM10 = 0x0007;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    SC_COMM10 = 0x0AAA;
    while (SC_COMM10 != 0x0BBB) {
    }
    take_fb();

    /* ---- 1. the 32X layer stays blank: nothing covers the white / green boot text ---- */
    SC_VDP_DISPMODE = 0x0000;
    if ((uint32_t)_bss_end > 0x06001F00) /* the engine's data starts at 0x06002000 */
        halt(0x5CE7);

    /* ---- 2. the splash (IMAGE.RAW) - Kobo's show_splash() goes here; off for now ---- */
#if SCD_SPLASH
#error "SCD_SPLASH: Kobo's show_splash() is not in yet"
#endif

    /* ---- 3. the engine, the data pack and the first scene into the cart ---- */
    {
        uint32_t engineLen = 0, dataLen = 0, sceneLen = 0;
        int r1, r2, r3;
        show_status(0x5C00);
        r1 = scd_load_file(FILE_ENGINE, &engineLen);
        if (r1 < 0)
            halt(0x5CE1);
        if (r1 > 0)
            halt(0x5CE2);
        if (engineLen > ENGINE_MAX)
            halt(0x5CE3);
        show_status(0x5C01);
        r2 = scd_load_file(FILE_DATA, &dataLen);
        if (r2 < 0)
            halt(0x5CE1);
        if (r2 > 0)
            halt(0x5CE2);
        if (dataLen > DATA_MAX)
            halt(0x5CE3);
        show_status(0x5C02);
        r3 = scd_load_file(FILE_FIRST_SCENE, &sceneLen);
        if (r3 < 0)
            halt(0x5CE1);
        if (r3 > 0)
            halt(0x5CE2);
        if (sceneLen > SCENE_MAX)
            halt(0x5CE3);
        show_status(0x5C03);
        BOOT_RECORD[1] = engineLen;
        BOOT_RECORD[2] = dataLen;
        BOOT_RECORD[3] = sceneLen;
    }

    /* ---- 4. the first scene's image, checked where it lies ---- */
    if (cart_verify(cart_read16, &r) != 0)
        halt(0x5CE3);
    show_status(0x5C04);

    /* ---- 5. hand over to the engine (interrupts stay on, as Kobo) ---- */
    BOOT_RECORD[0] = 0x53434442;                  /* "SCDB"; [1] engine, [2] data pack, [3] scene bytes */
    /* R1 fix: the routine's address in [6]. [4] is 0x06001F10 = VBLANK_COUNT - the vertical-blank
       interrupt counted it up (the console's 5CEA at PC 0x06000851 = scd_load_file + 5 vblanks: the
       first scene change the engine made itself, never tried on hardware before R1). */
    BOOT_RECORD[6] = (uint32_t)scd_load_file;      /* the engine loads scenes through this (SDRAM) */
    BOOT_RECORD[5] = FILE_FIRST_SCENE;             /* the scene now in the cart */
    BOOT_RECORD[7] = (uint32_t)&scdSampling;       /* R1C: the engine switches the PC sampler on / off */
    show_status(0x5C05);
    SC_CCR = 0x11; /* purge + enable the cache: the engine runs from the cart */
    { /* S7e6: PC sampling from here on (vector 70 = V blank; Kobo's handler stays, chained) */
        volatile uint32_t *vbr = (volatile uint32_t *)0x06000000;
        scdOrigVBI             = vbr[70];
        vbr[70]                = (uint32_t)scd_vbi_sample;
        scdSampling            = 0; /* S7e7: the sampler has named the loop; line 10 is for the engine's readings */
    }
    ((void (*)(void))ENGINE_ENTRY)();
    halt(0x5CE6);
    return 0;
}
