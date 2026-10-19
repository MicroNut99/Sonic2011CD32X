#!/usr/bin/env python3
"""make_cart_test.py OUT: the cart test's file (REV R4E) - KOBOGFX.BIN for the disc.

The Options Menu's CART TEST (cd32x/engine/plat_mars.cpp plat_cart_test) has the Sub-CPU load chunk p of this file
into page p of MicroNut99's 8 / 16 MB cart (window 1, cart 0x200000 + p * 32 KB; the file's number 16 is Kobo's
graphics pack's, which the Sub-CPU puts at cart 0x200000), then reads every page back on the SH2. 8 chunks of 32 KB:
big-endian 32-bit word i of chunk c = (i * 0x9E3779B1) ^ (0x43540000 | c << 8 | c) - the SH2 computes the same words
(cart_pattern). Not made from the game's data."""
import struct
import sys

CHUNKS, CHUNK = 8, 0x8000


def pattern(c, i):
    return ((i * 0x9E3779B1) ^ (0x43540000 | c << 8 | c)) & 0xFFFFFFFF


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = bytearray()
    for c in range(CHUNKS):
        out += struct.pack(">%dI" % (CHUNK // 4), *[pattern(c, i) for i in range(CHUNK // 4)])
    with open(sys.argv[1], "wb") as f:
        f.write(out)
    print("%s: the cart test's file, %d chunks of 32 KB" % (sys.argv[1], CHUNKS))


if __name__ == "__main__":
    main()
