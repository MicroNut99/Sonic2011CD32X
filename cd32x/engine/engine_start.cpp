// PORT32X S7b: the engine image's entry - the first code in IMAGE.RAW (cart 0x02000000). The boot
// loader (cd32x/sh2_engboot.c) jumps here; its stack stays the stack. S7b5: interrupts stay ON (as
// in Kobo) - Kobo's handlers stay in the loader's SDRAM area, below the engine's data. Each step
// posts a status code in COMM12 (Kobo's Sub-CPU shows it as STATE:xxxx).
//   1. copy the initial data from the cart into SDRAM, clear the zeroed arrays; R1V: copy the code that runs
//      every frame (.sdtext) from the cart into SDRAM
//   2. run the C++ static constructors (they bind the cart parts: the stage image is already there)
//   3. the engine's main()
// Each step shows in the status box (bottom right of the 32X layer).
#include "../../engine/RetroEngine.hpp"

void scd_status(int row, const char *t, int color);
#define STATE(code) (*(volatile unsigned short *)0x2000402C = (unsigned short)(code))
int main(int argc, char *argv[]);

extern "C" {
/* S7e5: an SH2 exception screen (OpenJazz addendum). Kobo's crt0 handler is "rte": after an address error
   the SH2 returns to the faulting instruction and faults again forever - a silent hang. The engine writes
   its own handlers into the SDRAM vector table (VBR 0x06000000) at start-up; Kobo's start-up code is
   unchanged. On a fault: STATE 5CEA address error / 5CEB illegal instruction / 5CEC slot / 5CED DMA
   address error, and the box shows PC (where) and PR (the caller). */
void scd_exc_show(unsigned int kind, unsigned int pc, unsigned int pr);
void scd_exc_illegal(void);
void scd_exc_slot(void);
void scd_exc_address(void);
void scd_exc_dma(void);
__asm__(".text\n"
        "    .align 2\n"
        "_scd_exc_illegal: mov #11,r4\n bra _scd_exc_common\n nop\n"
        "_scd_exc_slot:    mov #12,r4\n bra _scd_exc_common\n nop\n"
        "_scd_exc_address: mov #10,r4\n bra _scd_exc_common\n nop\n"
        "_scd_exc_dma:     mov #13,r4\n bra _scd_exc_common\n nop\n"
        "_scd_exc_common:\n"
        "    mov.l @r15,r5\n"      /* the PC the exception saved */
        "    sts pr,r6\n"           /* the caller */
        "    mov.l 1f,r0\n"
        "    jmp @r0\n"
        "    nop\n"
        "    .align 2\n"
        "1:  .long _scd_exc_show\n");
extern char scd_data_load[], scd_data_start[], scd_data_end[], scd_bss_start[], scd_bss_end[];
extern char scd_sdtext_load[], scd_sdtext_start[], scd_sdtext_end[];
extern void (*scd_init_start[])(void), (*scd_init_end[])(void);
void scd_engine_entry(void) __attribute__((section(".text.entry"), used));
}

void scd_engine_entry(void)
{
    for (char *s = scd_data_load, *d = scd_data_start; d < scd_data_end;) *d++ = *s++;
    for (char *d = scd_bss_start; d < scd_bss_end;) *d++ = 0;
    /* R1V: the hot code into SDRAM (nothing in it has run yet); then the cache starts empty - none of its lines
       can hold what those SDRAM addresses had before the copy */
    for (char *s = scd_sdtext_load, *d = scd_sdtext_start; d < scd_sdtext_end;) *d++ = *s++;
    *(volatile unsigned char *)0xFFFFFE92 = 0x11; /* purge + enable the cache (as the loader left it: 01) */
    { /* S7e5: our exception handlers into the SDRAM vector table (Kobo's crt0 stays unchanged) */
        volatile unsigned int *vbr = (volatile unsigned int *)0x06000000;
        vbr[4]  = (unsigned int)(unsigned long)scd_exc_illegal; /* illegal instruction */
        vbr[6]  = (unsigned int)(unsigned long)scd_exc_slot;    /* invalid slot instruction */
        vbr[9]  = (unsigned int)(unsigned long)scd_exc_address; /* CPU address error */
        vbr[10] = (unsigned int)(unsigned long)scd_exc_dma;     /* DMA address error */
    }
    STATE(0x5C10); /* S7c: progress only as STATE (the 32X layer stays blank - OpenLara) */
    for (void (**f)(void) = scd_init_start; f < scd_init_end; ++f) (*f)();
    STATE(0x5C11);
    static char name[] = "soniccd";
    static char *argv[] = { name, 0 };
    main(1, argv);
    STATE(0x5CE6);
    scd_status(1, "MAIN RETURNED", 3);
    for (;;) {
    }
}

static char *exc_hex(char *p, unsigned int v)
{
    for (int i = 7; i >= 0; --i) {
        int n = v & 15;
        p[i]  = (char)(n < 10 ? '0' + n : 'A' + n - 10);
        v >>= 4;
    }
    p[8] = 0;
    return p + 8;
}
extern "C" void scd_exc_show(unsigned int kind, unsigned int pc, unsigned int pr)
{
    char t[16];
    STATE(0x5CE0 | (kind & 0xF));
    t[0] = 'P', t[1] = 'C', t[2] = ' ';
    exc_hex(t + 3, pc);
    scd_status(0, t, 3);
    t[0] = 'P', t[1] = 'R', t[2] = ' ';
    exc_hex(t + 3, pr);
    scd_status(1, t, 3);
    for (;;) {
    }
}
