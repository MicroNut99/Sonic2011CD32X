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


# ---------------------------------------------------------------------------------------------------------------------
# REV R5G: THE SPECIAL STAGES' SKY ON THE GENESIS - the two background layers (DrawHLineScrollLayer: 512 x 256 each, one
# x per screen line) as planes A and B with a line's own h-scroll, as the 1993 game shows its sky; special stage 1's
# swirl (its Draw3DScene: SSBG1.gif, 16 pictures of 128 x 128, 5 quads side by side, sheared line by line) on plane B
# instead of layer 1 (the swirl covers it, lines 0-127). Appended to FLOOR<n>.BIN after the Word RAM image:
#   SKY_EXTRA bytes: 'SKY2', u16 flags (1: the swirl), u16 0, u16 the swirl's slots (240), u16 its pictures (16),
#     u16 pieces of buffer 0, of buffer 1 (<= 4 each), at 16 / 40 the pieces (u16 VRAM address, u16 first slot, u16
#     slots), at 64 the 48 colours' palette indices (CRAM 0-47: lines 0-2; 0 unused), at 160 u32 the swirl's offset in
#     the file, u32 its bytes; at 0x100 the VRAM blocks {u16 address, u16 words, the words} up to {0, 0}
#   (special stage 1) the swirl: 16 pictures x 240 cells (rows 0-14, columns 0-15 of the picture, row after row)
# The video RAM (cd32x/sub_main.c floor_begin): the floor's buffers 0x0040 / 0x4160; window tables 0xA000 / 0xC000
# (rows 15-27); plane A 0xC000 (rows 0-14: layer 2), plane B 0xE000 (rows 0-27: layer 1 - R5H, under the window too -
# or rows 0-14: the swirl's buffer 0, rows 15-27 empty), special stage 1's plane B' 0xA000 (the swirl's buffer 1); sprites 0xF800 (none), h-scroll 0xFC00; the
# cells in what is left (SKY_FREE).
SKY_EXTRA = 0x8000
SKY_FREE = [(0x8260, 237), (0xA000, 60), (0xAE00, 16), (0xB000, 128), (0xCE00, 16), (0xD000, 128), (0xEE00, 16),
            (0xF000, 64), (0xF820, 31), (0xFF80, 4)]


def sky_backgrounds(d):
    """Backgrounds.bin -> the layers: dict xs, ys, layout (chunk numbers), the line scroll list."""
    n, p = d[0], 1
    for _ in range(2):
        p += 1 + 4 * d[p]
    out = []
    for _ in range(n):
        xs, ys = d[p], d[p + 1]
        p += 6
        while True:
            if d[p] == 0xFF:
                if d[p + 1] == 0xFF:
                    p += 2
                    break
                p += 3
            else:
                p += 1
        lay = []
        for _ in range(xs * ys):
            lay.append(((d[p] << 8) | d[p + 1]) & 0x3FF)
            p += 2
        out.append({'xs': xs, 'ys': ys, 'lay': lay})
    return out


def sky_render(data, folder, L):
    """a background layer's pixels (palette indices of the stage's 16x16Tiles.gif), rows of 512"""
    gw, gh, pal, pix = rp.gif_decode(data.read(f'Data/Stages/{folder}/16x16Tiles.gif'))
    ch = chunks(data.read(f'Data/Stages/{folder}/128x128Tiles.bin'))
    W, H = L['xs'] * 128, L['ys'] * 128
    img = [[0] * W for _ in range(H)]
    for cy in range(L['ys']):
        for cx in range(L['xs']):
            c = L['lay'][cy * L['xs'] + cx]
            for t in range(64):
                tile, fl = ch[c * 64 + t]
                tx, ty = cx * 128 + (t % 8) * 16, cy * 128 + (t // 8) * 16
                for y in range(16):
                    sy = 15 - y if fl & FLIP_Y else y
                    row = pix[(tile * 16 + sy) * gw:(tile * 16 + sy) * gw + 16]
                    if fl & FLIP_X:
                        row = row[::-1]
                    img[ty + y][tx:tx + 16] = row
    return img, pal


def sky_lines(sets, rgb):
    """the colours (palette indices) into lines of 15 so that each cell's colours are in one line: greedy, biggest
    sets first; when it does not fit in 3 lines, the closest two colours (RGB) are merged and it is tried again.
    -> (lines: lists of indices, merged: {index: the index it shows})"""
    merged = {}
    while True:
        S = [frozenset(merged.get(c, c) for c in s) for s in sets]
        lines = []
        ok = True
        for cs in sorted(set(S), key=lambda x: (-len(x), sorted(x))):
            if any(cs <= ln for ln in lines):
                continue
            best = None
            for k, ln in enumerate(lines):
                g = len(cs - ln)
                if len(ln) + g <= 15 and (best is None or g < best[0]):
                    best = (g, k)
            if best is not None:
                lines[best[1]] |= cs
            elif len(lines) < 3 and len(cs) <= 15:
                lines.append(set(cs))
            else:
                ok = False
                break
        if ok:
            return [sorted(ln) for ln in lines], merged
        used = sorted(set(c for s in S for c in s))
        a, b = min(((x, y) for i, x in enumerate(used) for y in used[i + 1:]),
                   key=lambda xy: sum((rgb[xy[0]][k] - rgb[xy[1]][k]) ** 2 for k in range(3)))
        for c in list(merged):
            if merged[c] == b:
                merged[c] = a
        merged[b] = a


def sky_file(data, folder, pos):
    """REV R5G: the SKY_EXTRA part of FLOOR<pos + 1>.BIN and the swirl's pictures (special stage 1)"""
    L = sky_backgrounds(data.read(f'Data/Stages/{folder}/Backgrounds.bin'))
    swirl = folder == 'SS1' and pos == 0
    l1, pal = sky_render(data, folder, L[0])
    l2 = sky_render(data, folder, L[1])[0] if len(L) > 1 and L[1]['xs'] else None
    rgb = [tuple(c) for c in pal]
    # the cells: plane B rows 0-14 (layer 1, or the swirl's pictures), plane A rows 0-14 (layer 2)
    def cells_of(img, rows, cols):
        return {(r, c): tuple(tuple(img[r * 8 + y][c * 8:c * 8 + 8]) for y in range(8)) for r in rows for c in cols}
    if swirl:
        sw, swh, swpal, swpix = rp.gif_decode(data.read('Data/Sprites/Special/SSBG1.gif'))
        pics = []
        for f in range(16):
            fx, fy = (f & 3) * 128, (f >> 2) * 128
            img = [swpix[(fy + y) * sw + fx:(fy + y) * sw + fx + 128] for y in range(120)]
            pics.append([cells_of(img, [r], [c])[(r, c)] for r in range(15) for c in range(16)])
        bcells = None
    else:  # REV R5H: plane B rows 0-27 - layer 1 under the window too (the chip's floor in front of it: its see-through
        bcells = cells_of(l1, range(28), range(64))  # pixels show layer 1, as the engine's; the 32X draws none of it)
    acells = cells_of(l2, range(15), range(64)) if l2 else {}
    sets = [frozenset(x for row in cl for x in row) - {0} for cl in list(acells.values()) + list((bcells or {}).values())]
    if swirl:
        sets += [frozenset(x for row in cl for x in row) - {0} for p in pics for cl in p]
    sets = [s for s in sets if s]
    lines, merged = sky_lines(sets, rgb)
    line_of = lambda cs: next(k for k, ln in enumerate(lines) if cs <= set(ln))
    # the cells' pixels: 4 bits, the colour's place in its line (1-15)
    def cell_bytes(cl, k):
        nib = {c: lines[k].index(c) + 1 for c in lines[k]}
        b = bytearray(32)
        for y in range(8):
            for x in range(4):
                a0, a1 = merged.get(cl[y][2 * x], cl[y][2 * x]), merged.get(cl[y][2 * x + 1], cl[y][2 * x + 1])
                b[y * 4 + x] = (nib[a0] if a0 else 0) << 4 | (nib[a1] if a1 else 0)
        return bytes(b)
    def cs_of(cl):
        return frozenset(merged.get(x, x) for row in cl for x in row) - {0}
    free = [list(f) for f in SKY_FREE if not (swirl and f[0] == 0xA000)]
    def take(n):  # n cells from the free list: [(address, count)]
        got = []
        while n:
            f = max(free, key=lambda f: f[1]) if not got else next((f for f in free if f[1]), None)
            if f is None or f[1] == 0:
                raise SystemExit(f'{folder}: the sky does not fit in video RAM')
            k = min(n, f[1])
            got.append((f[0], k))
            f[0] += 32 * k
            f[1] -= k
            n -= k
        return got
    blocks = []
    def plane(cells, base):  # deduplicated cells (flips) -> name table rows 0-14 (R5H: plane B rows 0-27) at base
        names = [0] * (max(r for r, c in cells) + 1 if cells else 15) * 64
        store, out = {}, []
        for (r, c), cl in cells.items():
            cs = cs_of(cl)
            if not cs:
                continue
            k = line_of(cs)
            v = cell_bytes(cl, k)
            rows = [v[y * 4:y * 4 + 4] for y in range(8)]
            hf = lambda rs: [bytes(((b & 15) << 4 | b >> 4) for b in r[::-1]) for r in rs]
            for w, fl in ((v, 0), (b''.join(hf(rows)), 0x0800), (b''.join(rows[::-1]), 0x1000), (b''.join(hf(rows[::-1])), 0x1800)):
                if w in store:
                    names[r * 64 + c] = (k << 13) | fl | store[w]
                    break
            else:
                out.append(v)
                store[v] = len(out)  # (placeholder: the slot, numbered once the cells have their places)
                names[r * 64 + c] = (k << 13) | len(out)
        places = []
        for a, n in take(len(out)):
            places += [a // 32 + i for i in range(n)]
        fix = [0] + places
        names = [(w & 0xF800) | fix[w & 0x7FF] if w & 0x7FF else 0 for w in names]
        # the cells into their places, run by run
        i = 0
        while i < len(out):
            j = i
            while j + 1 < len(out) and places[j + 1] == places[j] + 1:
                j += 1
            blocks.append((places[i] * 32, b''.join(out[i:j + 1])))
            i = j + 1
        blocks.append((base, b''.join(struct.pack('>H', w) for w in names)))
        return len(out)
    head = bytearray(0x100)
    swirl_bytes = b''
    if swirl:
        k = line_of(frozenset(merged.get(c, c) for p in pics for cl in p for row in cl for c in row) - {0})
        def take_spared(n):  # the swirl's pieces go in by DMA each picture change: a spare cell before each piece (the
            got = []         # DMA's first word lands there - cd32x/sub_main.c flr_dma)
            while n:
                f = max(free, key=lambda f: f[1])
                if f[1] < 2:
                    raise SystemExit(f'{folder}: the swirl does not fit in video RAM')
                k = min(n, f[1] - 1)
                got.append((f[0] + 32, k))
                f[0] += 32 * (k + 1)
                f[1] -= k + 1
                n -= k
            return got
        bufs = [take_spared(240), take_spared(240)]
        for b, (base, at) in enumerate(((0xE000, 16), (0xA000, 40))):
            slots = []
            first = 0
            for a, n in bufs[b]:
                struct.pack_into('>HHH', head, at + 6 * len(slots), a, first, n)
                slots.append((a, first, n))
                first += n
            place = []
            for a, n in bufs[b]:
                place += [a // 32 + i for i in range(n)]
            names = [0] * (15 * 64)
            for r in range(15):
                for c in range(64):
                    names[r * 64 + c] = (k << 13) | place[r * 16 + (c & 15)]
            blocks.append((base, b''.join(struct.pack('>H', w) for w in names)))
            struct.pack_into('>H', head, 12 + 2 * b, len(slots))
        swirl_bytes = b''.join(cell_bytes(cl, k) for p in pics for cl in p)
        nb = 480
    else:
        nb = plane(bcells, 0xE000)
    na = plane(acells, 0xC000) if acells else 0
    struct.pack_into('>4sHHHH', head, 0, b'SKY2', 1 if swirl else 0, 0, 240 if swirl else 0, 16 if swirl else 0)
    for k, ln in enumerate(lines):
        for j, c in enumerate(ln):
            struct.pack_into('>H', head, 64 + 2 * (16 * k + j + 1), c)
    body = bytearray()
    for a, b in blocks:
        body += struct.pack('>HH', a, len(b) // 2) + b
    body += struct.pack('>HH', 0, 0)
    ext = bytes(head) + bytes(body)
    assert len(ext) <= SKY_EXTRA, (folder, len(ext))
    ext += bytes(SKY_EXTRA - len(ext))
    if swirl:
        e = bytearray(ext)
        struct.pack_into('>II', e, 160, 0x40000 + SKY_EXTRA, len(swirl_bytes))
        ext = bytes(e) + swirl_bytes
    info = f'sky: {len(lines)} colour lines, {sum(len(x) for x in lines)} colours' + (f' ({len(merged)} merged)' if merged else '')
    info += f', plane A {na} cells, plane B ' + ('the swirl 2 x 240 cells' if swirl else f'{nb} cells')
    return ext, info


def main():
    data = sr.Data(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    gc = sr.parse_gameconfig(data.read('Data/Game/GameConfig.bin'))
    specials = gc['lists'][3]
    for pos, st in enumerate(specials):
        folder, act = st[0], st[1]  # (folder, its act id, its name)
        img, n, colours, merged = floor_file(data, folder, f'Act{act}.bin')
        sky, info = sky_file(data, folder, pos)  # REV R5G
        name = f'FLOOR{pos + 1}.BIN'
        open(os.path.join(out, name), 'wb').write(img + sky)
        print(f'{name}: {folder} {n - 1} stamps, {len(colours)} colours' + (f', {len(merged)} merged' if merged else '') + '; ' + info)
    img, n, colours, pcol, scol, ns = title_file(data)  # REV R5E: the title's clouds (R5F: and the rest of its picture)
    open(os.path.join(out, 'FLOOR9.BIN'), 'wb').write(img)
    print(f'FLOOR9.BIN: the title\'s clouds, {n - 1} stamps, {len(colours)} colours; the planet, {len(pcol)} colours; '
          f'the mountains and the water, {ns} cells, {len(scol)} colours')


if __name__ == '__main__':
    main()
