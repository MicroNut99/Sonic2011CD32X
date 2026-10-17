#!/usr/bin/env python3
"""PORT32X REV R4B: the loading picture - the game's title screen with a loading bar where PRESS START shows.

  tools/scd_splash.py <Data.rsdk> <out IMAGE.RAW> [--png preview.png]

The 32X layer shows it while the Sub-CPU loads, the Genesis display off (command 52): at power-on the boot loader
(cd32x/sh2_engboot.c) loads it from the disc into the frame buffer (command 51), between the scenes the engine
(cd32x/engine/plat_mars.cpp) copies it from the cart - build_game_data.py appends it to SCDDATA.PAK. Made from your
Data.rsdk: the PC host engine (host/soniccd_host) plays the title screen (list 0, scene 0) and saves frame 560, a
frame with PRESS START blinked off.

IMAGE.RAW, laid out as command 51 puts it into the frame buffer (from byte 0x200 on, cd32x/sub_main.c):
     0  224 x 320 bytes: the pixels, palette numbers (0 is not used: the 32X shows colour 0x0000 see-through)
 71680  256 x u16, big-endian: the 32X colours (0BBBBBGGGGGRRRRR, bit 15 set: in front of the Genesis picture)
 72192  'BAR1', u16 the first segment's colour number, u16 segments, u16 empty colour, u16 full colour: the bar's
        segments are colours of their own - the loader and the engine fill the bar by setting those colours to the
        full colour, nothing is drawn while loading
"""
import os, struct, subprocess, sys, tempfile, zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
W, H = 320, 224
FRAME = 560                    # PRESS START is off from 548 to 572 (it blinks every 60 frames from 156 on)
PIXELS = W * H                 # 71680
PAL_AT = PIXELS
BAR_AT = PIXELS + 512
SIZE = BAR_AT + 12             # 72204
SEG_FIRST, SEGS = 224, 32      # the bar's segments: colours 224-255
BAR_X, BAR_Y, BAR_W, BAR_H = 96, 201, 128, 8   # inside the outline; PRESS START is x 117-202, y 200-209
SEG_W = BAR_W // SEGS


def c32x(rgb):
    """an RGB colour -> the 32X colour with bit 15 (in front of the Genesis); never black 0x8000 (0x0000 is see-through
    on the 32X - the plain colour 0 is kept clear of it as the engine does: 0x0400)"""
    r, g, b = (v >> 3 for v in rgb)
    c = 0x8000 | b << 10 | g << 5 | r
    return 0x8400 if c == 0x8000 else c


def read_png(path):
    """the host's frames (8-bit RGB PNG, host/plat_host.cpp savePNG) as rows of RGB tuples - no Pillow needed"""
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", path
    p, idat, w = 8, b"", 0
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8])
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", d[p + 8:p + 18])
            assert depth == 8 and ctype == 2, (depth, ctype)
        elif t == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    raw, rows, prev, bpr = zlib.decompress(idat), [], bytearray(3 * w), 3 * w
    for y in range(h):
        f, line = raw[y * (bpr + 1)], bytearray(raw[y * (bpr + 1) + 1:(y + 1) * (bpr + 1)])
        for i in range(bpr):  # the PNG filters (the host writes 0, none)
            a, b, c = line[i - 3] if i >= 3 else 0, prev[i], prev[i - 3] if i >= 3 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[3 * x:3 * x + 3]) for x in range(w)])
        prev = line
    return rows


def title_frame(data_path):
    """the title screen's frame FRAME as rows of RGB tuples (the PC host engine, no pad: nothing pressed)"""
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "settings.ini"), "w") as f:
            f.write(f"[Dev]\nStartingCategory=0\nStartingScene=0\nDataFile={os.path.abspath(data_path)}\n")
        fr = os.path.join(d, "frames")
        os.makedirs(fr)
        env = dict(os.environ, SCD32X_MAX_FRAMES=str(FRAME), SCD32X_FRAMES_DIR=fr, SCD32X_SAVE_EVERY=str(FRAME))
        for k in ("SCD32X_PAD", "SCD32X_MDCHECK", "SCD32X_PLAYER", "SCD32X_DEVMENU"):
            env.pop(k, None)
        with open(os.path.join(d, "out.txt"), "w") as out:
            subprocess.run([os.path.join(ROOT, "host", "soniccd_host")], cwd=d, env=env, stdout=out, stderr=subprocess.STDOUT,
                           timeout=600)
        png = os.path.join(fr, "f%06d.png" % FRAME)
        if not os.path.exists(png):
            sys.exit("scd_splash: the title screen's frame %d was not saved (%s)" % (FRAME, png))
        rows = read_png(png)
        assert len(rows) == H and len(rows[0]) == W, (len(rows[0]), len(rows))
        return rows


def make(rows):
    """-> IMAGE.RAW's bytes and the picture as RGB rows (the preview)"""
    outline, empty, full = (0, 0, 0), (0, 0, 98), (230, 226, 0)  # PRESS START's outline and letters, the title's navy
    pic = [list(r) for r in rows]
    seg = {}
    for y in range(BAR_Y - 1, BAR_Y + BAR_H + 1):
        for x in range(BAR_X - 1, BAR_X + BAR_W + 1):
            if BAR_X <= x < BAR_X + BAR_W and BAR_Y <= y < BAR_Y + BAR_H:
                seg[(x, y)] = (x - BAR_X) // SEG_W
                pic[y][x] = empty
            else:
                pic[y][x] = outline
    colours = sorted({c for r in pic for c in r})
    if len(colours) > SEG_FIRST - 1:
        sys.exit("scd_splash: the title screen has %d colours - at most %d fit next to the bar" % (len(colours), SEG_FIRST - 1))
    index = {c: 1 + i for i, c in enumerate(colours)}
    pal = [0] * 256
    for c, i in index.items():
        pal[i] = c32x(c)
    for k in range(SEGS):
        pal[SEG_FIRST + k] = c32x(empty)
    out = bytearray()
    for y in range(H):
        for x in range(W):
            out.append(SEG_FIRST + seg[(x, y)] if (x, y) in seg else index[pic[y][x]])
    out += struct.pack(">256H", *pal)
    out += b"BAR1" + struct.pack(">4H", SEG_FIRST, SEGS, c32x(empty), c32x(full))
    assert len(out) == SIZE
    return bytes(out), pic


def preview(pic, path, filled=SEGS * 2 // 3):
    from PIL import Image  # (Pillow: only for this preview, not for the build)
    im = Image.new("RGB", (W, H))
    flat = []
    for y in range(H):
        for x in range(W):
            c = pic[y][x]
            if BAR_X <= x < BAR_X + BAR_W and BAR_Y <= y < BAR_Y + BAR_H and (x - BAR_X) // SEG_W < filled:
                c = (230, 226, 0)
            flat.append(tuple(((v >> 3) * 255 + 15) // 31 for v in c))  # as the 32X shows it (5 bits a channel)
    im.putdata(flat)
    im.save(path)


def main():
    args = sys.argv[1:]
    png = None
    if "--png" in args:
        i = args.index("--png")
        png = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    raw, pic = make(title_frame(args[0]))
    open(args[1], "wb").write(raw)
    if png:
        preview(pic, png)
    print("%s: %d bytes (the title screen, frame %d, %d colours + the bar's %d)" % (
        args[1], len(raw), FRAME, len({c for r in pic for c in r}), SEGS))


if __name__ == "__main__":
    main()
