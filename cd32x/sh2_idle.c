/* PORT32X: round S3b - the master SH2's whole job in the plane demo: answer the start
 * handshake (COMM10 0x0AAA / 0x0BBB, as Kobo), switch the 32X picture off so only the Mega
 * Drive picture shows, and idle. Started by Kobo's crt0.s (kobo/sh2). */
#include <stdint.h>
#include "32x.h"

const char sh2_build_marker[] = "SH2-VS3B"; /* the Sub-CPU looks for "SH2-V" */

#define SC_COMM10 (*(volatile uint16_t *)0x2000402A)
#define SC_CMD_CLR (*(volatile uint16_t *)0x2000401A)

void pri_vbi_handler(void) {}
void pri_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_dma1_handler(void) {}
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;

void secondary(void)
{
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    while (1) {
    }
}

int main(void)
{
    SC_COMM10      = 0x0004;
    MARS_SYS_COMM0 = 0;
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    SC_COMM10 = 0x0AAA; /* "SH2 ready" */
    while (SC_COMM10 != 0x0BBB) {
    }
    MARS_VDP_DISPMODE = MARS_VDP_MODE_OFF; /* 32X layer off: the Mega Drive planes only */
    while (1) {
    }
}
