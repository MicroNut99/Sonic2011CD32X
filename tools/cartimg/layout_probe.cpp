// PORT32X S6a: the cart layout numbers (cart_layout.h) as data. Built twice: by the PC compiler (run:
// prints them) and by sh-elf-g++ (not run: the numbers are read out of the object file).
#include "cart_layout.h"
extern "C" const int scd_layout_probe[] = { SCD_LAYOUT_PROBE_VALUES };
#ifndef SCD_PROBE_NO_MAIN
#include <stdio.h>
int main()
{
    for (unsigned i = 0; i < sizeof(scd_layout_probe) / sizeof(int); ++i) printf("%d\n", scd_layout_probe[i]);
    return 0;
}
#endif
