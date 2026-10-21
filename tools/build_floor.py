#!/usr/bin/env python3
"""REV R5: build_floor.py <Data.rsdk> <out dir> - each special stage's rotating floor for the Sega CD's graphics chip
(the stamp / rotation ASIC), as the 1993 game draws it: FLOOR<n>.BIN for SS1 - SS8 (the special stage list, as
GameConfig.bin lists it).

The floor is the stage's act layout (layer 0) as the engine's Init3DFloorBuffer(0) lays it out: 256 x 256 tiles of
16 x 16 pixels (4096 x 4096), each tile a 128 x 128 chunk entry (its tile, its flip). On the chip that is a stamp map
of 256 x 256 stamps of 16 x 16 pixels (map size 4096, stamp size 16) - one stamp per tile, the flips in the map entry:
FLIP_X = H flip, FLIP_Y = H flip + 180 degrees, FLIP_XY = 180 degrees. Stamp 0 is the chip's "nothing" (an empty tile,
or a tile with no pixel drawn).

The chip draws 4 bits a pixel: the floor's colours (palette indices of the stage's 16x16Tiles.gif, colour 0 is
see-through) become 1-15; a floor with more than 15 colours has its closest pair (by the GIF's RGB) merged until 15
are left (SS1 and SS4: 16 colours, one merge each).

FLOOR<n>.BIN (big-endian), laid out as the Sub-CPU puts it into Word RAM (2 Mbit mode, 0x00000 - 0x3FFFF):
    0x00000  'FLR1', u16 stamps (including stamp 0), u16 colours (1-15), u16 palette index of each colour 1-15 (15
             words, 0 for the unused), u16 the merged colours, then zeros to 0x80
    0x00080  stamp 1, 2, ... (128 bytes each: the chip's 16 x 16 stamp, four 8 x 8 cells in the column order
             top-left, bottom-left, top-right, bottom-right, two pixels a byte, the high nibble first)
    ...      zeros up to 0x20000
    0x20000  the stamp map: 256 x 256 entries of 16 bits (row by row): bits 0-10 the stamp, 13 a 90 degree turn,
             14 a 180 degree turn, 15 H flip
The file is 0x40000 bytes: the Sub-CPU reads it straight into Word RAM (the stamps' half, then the map's)."""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk_pack as rp
import scd_report as sr

FLIP_X, FLIP_Y, FLIP_XY = 1, 2, 3


def chunks(d):
    out = []
    for i in range(len(d) // 3):
        e0, e1, e2 = d[3 * i], d[3 * i + 1], d[3 * i + 2]
        e0 &= 0x3F
        e0 &= 15  # (the visual plane: not the floor's business)
        out.append(((e0 & 3) << 8 | e1, e0 >> 2))
    return out


def act_layout(d):
    n = d[0]
    p = 1 + n + 5
    xs, ys = d[p], d[p + 1]
    p += 2
    lay = {}
    for y in range(ys):
        for x in range(xs):
            lay[(x, y)] = d[p] << 8 | d[p + 1]
            p += 2
    return xs, ys, lay


def floor_file(data, folder, act_name):
    ch = chunks(data.read(f'Data/Stages/{folder}/128x128Tiles.bin'))
    xs, ys, lay = act_layout(data.read(f'Data/Stages/{folder}/{act_name}'))
    gw, gh, pal, pix = rp.gif_decode(data.read(f'Data/Stages/{folder}/16x16Tiles.gif'))  # (the engine's own reading)
    px = lambda x, y: pix[y * gw + x]
    rgb = lambda i: tuple(pal[i]) if i < len(pal) else (0, 0, 0)
    # every map entry (Init3DFloorBuffer(0)): its tile and flip
    entries = {}
    for ey in range(256):
        for ex in range(256):
            row = ey >> 3
            c = lay.get((ex >> 3, row), 0) if row < ys and (ex >> 3) < xs else 0
            e = (c << 6) + (ex & 7) + ((ey & 7) << 3)
            entries[(ex, ey)] = ch[e] if e < len(ch) else (0, 0)
    tiles = sorted(set(t for t, _ in entries.values()))
    # the floor's colours
    used = set()
    for t in tiles:
        for y in range(16):
            for x in range(16):
                used.add(px(x, t * 16 + y))
    used.discard(0)
    colours = sorted(used)
    merged = {}
    while len(colours) > 15:  # the closest pair -> one (the first of the two stays)
        best = None
        for i in range(len(colours)):
            for j in range(i + 1, len(colours)):
                a, b = rgb(colours[i]), rgb(colours[j])
                dist = sum((a[k] - b[k]) ** 2 for k in range(3))
                if best is None or dist < best[0]:
                    best = (dist, i, j)
        _, i, j = best
        merged[colours[j]] = colours[i]
        del colours[j]
    nib = {c: k + 1 for k, c in enumerate(colours)}
    for a, b in merged.items():
        nib[a] = nib[b]
    nib[0] = 0
    # the stamps: one per tile with a pixel drawn (stamp 0: nothing)
    stamp_of, stamps = {}, [bytes(128)]
    for t in tiles:
        p = [[nib[px(x, t * 16 + y)] for x in range(16)] for y in range(16)]
        if not any(any(r) for r in p):
            stamp_of[t] = 0
            continue
        b = bytearray(128)
        for cx in range(2):  # the left column of cells, then the right
            for y in range(16):  # (a column: 16 rows of 8 pixels, 4 bytes a row)
                for x in range(8):
                    v = p[y][cx * 8 + x]
                    o = cx * 64 + y * 4 + (x >> 1)
                    b[o] |= v << 4 if (x & 1) == 0 else v
        stamp_of[t] = len(stamps)
        stamps.append(bytes(b))
    assert len(stamps) <= 0x10000 // 128, (folder, len(stamps))  # (they end below the image buffer at 0x10000)
    img = bytearray(0x40000)
    head = b'FLR1' + struct.pack('>HH', len(stamps), len(colours))
    head += b''.join(struct.pack('>H', c) for c in colours) + b'\0\0' * (15 - len(colours))
    head += struct.pack('>H', len(merged))
    img[0:len(head)] = head
    for k in range(1, len(stamps)):
        img[k * 128:k * 128 + 128] = stamps[k]
    for (ex, ey), (t, d) in entries.items():
        s = stamp_of.get(t, 0)
        w = s
        if s:
            if d == FLIP_X:
                w |= 0x8000
            elif d == FLIP_Y:
                w |= 0x8000 | 0x4000
            elif d == FLIP_XY:
                w |= 0x4000
        struct.pack_into('>H', img, 0x20000 + 2 * ((ey << 8) + ex), w)
    return bytes(img), len(stamps), colours, merged


def stamp_bytes(p):
    """16 x 16 nibbles (rows of 16) -> the chip's 128-byte stamp: the left column of cells, then the right"""
    b = bytearray(128)
    for cx in range(2):
        for y in range(16):
            for x in range(8):
                v = p[y][cx * 8 + x]
                b[cx * 64 + y * 4 + (x >> 1)] |= v << 4 if (x & 1) == 0 else v
    return bytes(b)


TTL_EXTRA = 0x4000  # REV R5F (R5E: 0x2000)


def title_file(data):
    """REV R5E: FLOOR9.BIN - the title's cloud ceiling for the chip (its Draw3DScene: Title/Clouds.gif on a 16 x 10 grid
    of squares 1024 x 512 world units, 64 x 64 texels each, x -8192 .. 8192, z 0 .. 5120, at y 720 below the camera's
    ... the world translated by (-512, 720, -scroll), turned 8 / 512 about X; projection 136 / 160, centre 160, 112).
    The map is 4096 x 4096: the texture (256 x 256) repeated over the grid's own extent - u 0 .. 1023, v 0 .. 639 - and
    nothing outside it, so past the grid's far edge the chip draws nothing (the sky shows). Each of lines 0-111: the
    texel at its first pixel (u 13.3), its step a pixel (5.11) and its v for scroll 0 (13.3 - the scroll adds itself:
    v = (z + scroll) / 8). Then the planet (Title.gif 257, 146, 120 x 104) as Genesis cells, colours line 1."""
    gw, gh, pal, pix = rp.gif_decode(data.read('Data/Sprites/Title/Clouds.gif'))
    colours = sorted(set(pix) - {0})
    assert len(colours) <= 15, colours
    nib = {c: k + 1 for k, c in enumerate(colours)}
    nib[0] = 0
    stamps, stamp_of = [bytes(128)], {}
    for sy in range(16):
        for sx in range(16):
            p = [[nib[pix[(sy * 16 + y) * gw + sx * 16 + x]] for x in range(16)] for y in range(16)]
            if not any(any(r) for r in p):
                stamp_of[(sx, sy)] = 0
                continue
            stamp_of[(sx, sy)] = len(stamps)
            stamps.append(stamp_bytes(p))
    img = bytearray(0x40000)
    head = b'FLR1' + struct.pack('>HH', len(stamps), len(colours))
    head += b''.join(struct.pack('>H', c) for c in colours) + b'\0\0' * (15 - len(colours))
    head += struct.pack('>H', 0)
    img[0:len(head)] = head
    for k in range(1, len(stamps)):
        img[k * 128:k * 128 + 128] = stamps[k]
    for row in range(40):                      # v 0 .. 639
        for col in range(64):                  # u 0 .. 1023
            struct.pack_into('>H', img, 0x20000 + 2 * ((row << 8) + col), stamp_of[(col & 15, row & 15)])
    # REV R5F: the rest of the title on the Genesis too ('TTL2', 16 KB after the Word RAM image) - the mountains and the
    # water on plane B (rows 12-27, colours line 1), the planet as 8 hardware sprites (colours line 2). The lines: 104
    # (window rows 0-12); 100-103 see-through (the mountains' first lines show there, from plane B)
    ext = bytearray(TTL_EXTRA)
    tw, th, tpal, tpix = rp.gif_decode(data.read('Data/Sprites/Title/Title.gif'))
    # the planet: Title.gif 257, 146, 120 x 104 at x 280 - its first 5 cell columns are on the screen (280-319)
    planet = [[tpix[(146 + y) * tw + 257 + x] for x in range(40)] for y in range(104)]
    pcol = sorted(set(c for r in planet for c in r) - {0})
    assert len(pcol) <= 15, pcol
    pn = {c: k + 1 for k, c in enumerate(pcol)}
    pn[0] = 0
    def cell_of(img, cy, cx, nmap):  # 8 x 8 pixels -> 32 bytes, 4 bits a pixel
        b = bytearray(32)
        for y in range(8):
            for x in range(4):
                b[y * 4 + x] = nmap[img[cy * 8 + y][cx * 8 + 2 * x]] << 4 | nmap[img[cy * 8 + y][cx * 8 + 2 * x + 1]]
        return bytes(b)
    pcells = []
    for (c0, w) in ((0, 4), (4, 1)):            # the sprites: columns 0-3 and 4, rows 0-3, 4-7, 8-11, 12
        for (r0, h) in ((0, 4), (4, 4), (8, 4), (12, 1)):
            for c in range(c0, c0 + w):         # (a sprite's cells column after column)
                for r in range(r0, r0 + h):
                    pcells.append(cell_of(planet, r, c, pn))
    assert len(pcells) == 65
    # plane B: lines 96-167 the mountains (Title.gif 256, 4, 176 x 68 at x 0 and 256, 77, 124 x 68 at x 196, y 100; their
    # sky colour 128 = the title's sky (0, 0, 96) see-through - the 32X's colour 0 shows it), lines 168-223 the water
    # (Title.gif rows 447-502, 512 wide: 14 strips of 4 lines, each its own h-scroll)
    plane = [[0] * 512 for _ in range(128)]     # lines 96-223
    for (sx, sy, cw, ch, dx) in ((256, 4, 176, 68, 0), (256, 77, 124, 68, 196)):
        for j in range(ch):
            for i in range(cw):
                v = tpix[(sy + j) * tw + sx + i]
                if v and v != 128 and dx + i < 320:
                    plane[4 + j][dx + i] = v
    for j in range(56):
        for i in range(512):
            plane[72 + j][i] = tpix[(447 + j) * tw + i]
    scol = sorted(set(c for r in plane for c in r) - {0})
    assert len(scol) <= 15, scol
    sn = {c: k + 1 for k, c in enumerate(scol)}
    sn[0] = 0
    scells, sidx, names = [], {}, [0] * (16 * 64)
    for cy in range(16):
        for cx in range(64):
            cell = cell_of(plane, cy, cx, sn)
            if not any(cell):
                continue
            rows = [cell[y * 4:y * 4 + 4] for y in range(8)]
            hf = lambda rs: [bytes(((b & 15) << 4 | b >> 4) for b in r[::-1]) for r in rs]
            variants = ((b''.join(rows), 0), (b''.join(hf(rows)), 0x0800), (b''.join(rows[::-1]), 0x1000),
                        (b''.join(hf(rows[::-1])), 0x1800))
            for v, fl in variants:
                if v in sidx:
                    names[cy * 64 + cx] = 0x2000 | fl | sidx[v]
                    break
            else:
                scells.append(cell)
                sidx[cell] = len(scells)          # (1-based: the Sub-CPU adds the first cell's number - 1)
                names[cy * 64 + cx] = 0x2000 | len(scells)
    assert 0x1340 + 32 * len(scells) <= TTL_EXTRA, len(scells)
    struct.pack_into('>4sHHHH', ext, 0, b'TTL2', 104, len(pcells), len(scells), 0)
    for k, c in enumerate(pcol):
        struct.pack_into('>H', ext, 12 + 2 * k, c)
    for k, c in enumerate(scol):
        struct.pack_into('>H', ext, 42 + 2 * k, c)
    # the lines (the engine's matrix values: 720 * 254 >> 8 = 714, 720 * 25 >> 8 = 70; sin, cos of 8: 25, 254)
    for sy in range(104):
        if sy >= 100:                           # (behind the mountains: see-through, plane B shows)
            struct.pack_into('>hhh', ext, 0x60 + 6 * sy, 0, 0, 0)
            continue
        a = 112 - sy
        D = 256.0 * (160 * 714 - 70 * a) / (254 * a + 160 * 25)
        zc = 70 + 254 * D / 256
        u0 = (512 + (0 - 160) * zc / 136 + 8192) / 16
        du = zc / (136 * 16)
        assert 0 <= u0 * 8 < 32768 and du * 2048 < 32768 and D < 32768, (sy, u0, du, D)
        struct.pack_into('>hhh', ext, 0x60 + 6 * sy, int(round(u0 * 8)), int(round(du * 2048)), int(round(D)))
    for k, w in enumerate(names):
        struct.pack_into('>H', ext, 0x300 + 2 * k, w)
    for k, c in enumerate(pcells):
        ext[0xB00 + 32 * k:0xB20 + 32 * k] = c
    for k, c in enumerate(scells):
        ext[0x1340 + 32 * k:0x1360 + 32 * k] = c
    return bytes(img) + bytes(ext), len(stamps), colours, pcol, scol, len(scells)


def main():
    data = sr.Data(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    gc = sr.parse_gameconfig(data.read('Data/Game/GameConfig.bin'))
    specials = gc['lists'][3]
    for pos, st in enumerate(specials):
        folder, act = st[0], st[1]  # (folder, its act id, its name)
        img, n, colours, merged = floor_file(data, folder, f'Act{act}.bin')
        name = f'FLOOR{pos + 1}.BIN'
        open(os.path.join(out, name), 'wb').write(img)
        print(f'{name}: {folder} {n - 1} stamps, {len(colours)} colours' + (f', {len(merged)} merged' if merged else ''))
    img, n, colours, pcol, scol, ns = title_file(data)  # REV R5E: the title's clouds (R5F: and the rest of its picture)
    open(os.path.join(out, 'FLOOR9.BIN'), 'wb').write(img)
    print(f'FLOOR9.BIN: the title\'s clouds, {n - 1} stamps, {len(colours)} colours; the planet, {len(pcol)} colours; '
          f'the mountains and the water, {ns} cells, {len(scol)} colours')


if __name__ == '__main__':
    main()
