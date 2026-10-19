#!/usr/bin/env python3
"""REV R4G - the preload table for the 16 MB cart, in SCDDATA.PAK.

usage: build_preload.py <game data dir>        (after build_game_data.py and build_mdg.sh; run it again any time)

The 16 MB cart (MicroNut99's, battery-backed) keeps the act scenes the game preloaded in its pages through power-off.
After a power-on the engine (cd32x/engine/plat_mars.cpp, "THE PRELOAD") must find out what each page holds and whether
it is whole, without reading the disc: it looks each page's scene image and act file up in this table, which the boot
loader brings into the cart with SCDDATA.PAK at every power-on.

One entry (16 bytes, big-endian) for every regular stage scene image (L1Snnn.IMG) and act file (G1Snnn.MDG) in the
folder:
    u16 file number as the engine asks the Sub-CPU for it (image 0x200 + nnn, act file 0x1200 + nnn)
    u16 kind (1 image, 2 act file)
    u32 length in bytes
    u32 sum: the loader's scd_cart_sum / the Sub-CPU's load_sum over the file padded with zeros to 2048 bytes (what a CD
        read puts in the cart: whole sectors)
    u32 fingerprint: FNV-1a over the file's identifying head - an image's header up to the end of its chunk CRC list
        ("SCHK"), an act file's first 4096 bytes
SCDDATA.PAK: [the data pack][the loading picture][the table][16 bytes 'PRE1', table offset, entries, 0][16 bytes 'SPL1'
footer, unchanged] - the loading picture's footer stays the last 16 bytes (plat_mars.cpp splash_image). Run again, it
replaces its own table.
"""
import glob
import os
import struct
import sys


def load_sum(data):
    """sh2_engboot.c scd_cart_sum / sub_main.c load_sum: big-endian 32-bit words"""
    a = b = 0
    n = len(data) // 4
    for w in struct.unpack(">%dI" % n, data[:4 * n]):
        a = (a + w) & 0xFFFFFFFF
        b = (b + a) & 0xFFFFFFFF
    return a ^ (((b << 13) | (b >> 19)) & 0xFFFFFFFF)


def fnv(data):
    """FNV-1a over 32-bit big-endian words (plat_mars.cpp fnv_words)"""
    h = 0x811C9DC5
    n = len(data) // 4
    for w in struct.unpack(">%dI" % n, data[:4 * n]):
        h = ((h ^ w) * 0x01000193) & 0xFFFFFFFF
    return h


def image_head(d):
    """an image's header up to the end of its chunk CRC list (build_game_data.py fixed_image)"""
    assert d[:4] == b"SCCB", "not a scene image"
    n = struct.unpack(">I", d[8:12])[0]
    hdr = 12 + 20 * n
    assert d[hdr:hdr + 4] == b"SCHK", "no chunk list"
    chunks = struct.unpack(">I", d[hdr + 4:hdr + 8])[0]
    return d[:hdr + 8 + 8 * chunks]


def entry(path, number, kind):
    d = open(path, "rb").read()
    padded = d + b"\0" * (-len(d) % 2048)
    head = image_head(d) if kind == 1 else d[:4096]
    return struct.pack(">HHIII", number, kind, len(d), load_sum(padded), fnv(head))


def main():
    gd = sys.argv[1]
    entries = []
    for p in sorted(glob.glob(os.path.join(gd, "L1S[0-9][0-9][0-9].IMG"))):
        entries.append(entry(p, 0x200 + int(os.path.basename(p)[3:6]), 1))
    for p in sorted(glob.glob(os.path.join(gd, "G1S[0-9][0-9][0-9].MDG"))):
        entries.append(entry(p, 0x1200 + int(os.path.basename(p)[3:6]), 2))
    fps = [e[12:16] for e in entries]
    assert len(set(fps)) == len(fps), "two files with the same fingerprint"
    pak = os.path.join(gd, "SCDDATA.PAK")
    s = open(pak, "rb").read()
    assert s[-16:-12] == b"SPL1", "SCDDATA.PAK without its loading picture footer"
    spl = s[-16:]
    body = s[:-16]
    if len(body) >= 16 and body[-16:-12] == b"PRE1":  # an earlier table: replace it
        off = struct.unpack(">I", body[-12:-8])[0]
        body = body[:off]
    body += b"\0" * (-len(body) & 3)
    off = len(body)
    out = body + b"".join(entries) + b"PRE1" + struct.pack(">3I", off, len(entries), 0) + spl
    assert len(out) <= 0x80000, "SCDDATA.PAK over its 512 KB slot (cart 0x300000-0x37FFFF)"
    open(pak, "wb").write(out)
    print("SCDDATA.PAK: the preload table, %d entries (%d images, %d act files), %d bytes in all" % (
        len(entries), sum(1 for e in entries if e[3] == 1), sum(1 for e in entries if e[3] == 2), len(out)))


if __name__ == "__main__":
    main()
