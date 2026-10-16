#!/usr/bin/env python3
"""PORT32X: read Sonic CD's Data.rsdk (RSDKv3 data pack) on the PC.

  tools/rsdk_pack.py list      Data.rsdk            every file with its size
  tools/rsdk_pack.py extract   Data.rsdk outdir     decrypt every file into outdir
  tools/rsdk_pack.py inventory Data.rsdk [report]   per-stage memory numbers (Markdown)

The format follows the engine's own reader (engine/Reader.cpp, ParseVirtualFileSystem
and FileRead); tools/test_rsdk_pack.sh checks this tool against that reader.

Pack layout (all integers little-endian):
  u32 headerSize, u16 dirCount
  dirCount x { u8 len, name[len] (each byte XOR ~len), u32 offset }   e.g. "Data/Stages/R11A/"
  at headerSize + offset, the files of that folder until the next folder's offset:
  { u8 len, name[len] (each byte inverted), u32 size, data[size] (encrypted) }
"""
import os, struct, sys

KEY_A = b"4RaS9D7KaEbxcp2o5r6t"
KEY_B = b"3tRaUxLmEaSn"


def _key_stream(size):
    """Yield (a, b, number, swap) for each byte, exactly as FileRead() steps them.
    Index 0 of each key is never used after the start values, as in the engine
    (eStringPosA starts at 1 or more)."""
    no = (size & 0x1FC) >> 2
    pb = (no % 9) + 1
    pa = (no % pb) + 1
    swap = 0
    while True:
        yield KEY_A[pa] if pa < len(KEY_A) else 0, KEY_B[pb] if pb < len(KEY_B) else 0, no, swap
        pa += 1
        pb += 1
        if pa <= 19 or pb <= 11:
            if pa > 19:
                pa = 1
                swap ^= 1
            if pb > 11:
                pb = 1
                swap ^= 1
        else:
            no = (no + 1) & 0x7F
            if swap:
                swap = 0
                pa = (no % 12) + 6
                pb = (no % 5) + 4
            else:
                swap = 1
                pa = (no % 15) + 3
                pb = (no % 7) + 1


def _swap(x):
    return ((x & 0xF) << 4) | (x >> 4)


def decrypt(data):
    out = bytearray(len(data))
    ks = _key_stream(len(data))
    for i, c in enumerate(data):
        a, b, no, swap = next(ks)
        x = c ^ b ^ no
        if swap:
            x = _swap(x)
        out[i] = (x ^ a) & 0xFF
    return bytes(out)


def encrypt(data):
    out = bytearray(len(data))
    ks = _key_stream(len(data))
    for i, c in enumerate(data):
        a, b, no, swap = next(ks)
        x = c ^ a
        if swap:
            x = _swap(x)
        out[i] = (x ^ b ^ no) & 0xFF
    return bytes(out)


class Pack:
    """All files of a pack: self.files = [(path, offset_of_data, size)]."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.raw = f.read()
        raw = self.raw
        header, ndirs = struct.unpack_from("<IH", raw, 0)
        pos = 6
        dirs = []
        for _ in range(ndirs):
            n = raw[pos]
            name = bytes(c ^ (~n & 0xFF) for c in raw[pos + 1:pos + 1 + n]).decode("latin-1")
            pos += 1 + n
            (off,) = struct.unpack_from("<I", raw, pos)
            pos += 4
            dirs.append((name, off))
        self.files = []
        for d, (name, off) in enumerate(dirs):
            end = dirs[d + 1][1] if d + 1 < len(dirs) else len(raw) - header
            p = header + off
            while p < header + end:
                n = raw[p]
                fname = bytes((~c) & 0xFF for c in raw[p + 1:p + 1 + n]).decode("latin-1")
                p += 1 + n
                (size,) = struct.unpack_from("<I", raw, p)
                p += 4
                self.files.append((name + fname, p, size))
                p += size

    def read(self, path):
        for name, off, size in self.files:
            if name.lower() == path.lower():
                return decrypt(self.raw[off:off + size])
        return None


def write_pack(path, files):
    """files: {"Data/Game/X.bin": bytes}. Used by the self-test only."""
    dirs = {}
    for name in sorted(files):
        d, f = name.rsplit("/", 1)
        dirs.setdefault(d + "/", []).append((f, files[name]))
    order = sorted(dirs)
    body = b""
    offsets = []
    for d in order:
        offsets.append(len(body))
        for f, data in dirs[d]:
            fb = f.encode("latin-1")
            body += bytes([len(fb)]) + bytes((~c) & 0xFF for c in fb) + struct.pack("<I", len(data)) + encrypt(data)
    table = b""
    for d, off in zip(order, offsets):
        db = d.encode("latin-1")
        table += bytes([len(db)]) + bytes(c ^ (~len(db) & 0xFF) for c in db) + struct.pack("<I", off)
    header = 6 + len(table)
    with open(path, "wb") as f:
        f.write(struct.pack("<IH", header, len(order)) + table + body)


def gif_size(data):
    if data and data[:3] == b"GIF" and len(data) >= 10:
        return struct.unpack_from("<HH", data, 6)
    return None


def gif_decode(data):
    """Returns (width, height, palette [(r,g,b)...], pixel indices bytes) of the first
    image, rows in screen order (interlaced GIFs are reordered), like the engine's
    ReadGifPictureData."""
    if not data or data[:3] != b"GIF":
        return None
    w, h, flags = struct.unpack_from("<HHB", data, 6)
    p = 13
    pal = []
    if flags & 0x80:
        n = 1 << ((flags & 7) + 1)
        pal = [tuple(data[p + 3 * i:p + 3 * i + 3]) for i in range(n)]
        p += 3 * n
    while p < len(data) and data[p] != 0x2C:  # skip extensions up to the image descriptor
        if data[p] == 0x21:
            p += 2
            while p < len(data) and data[p]:
                p += data[p] + 1
            p += 1
        else:
            p += 1
    if p >= len(data):
        return None
    iw, ih, iflags = struct.unpack_from("<HHB", data, p + 5)
    p += 10
    if iflags & 0x80:
        n = 1 << ((iflags & 7) + 1)
        pal = [tuple(data[p + 3 * i:p + 3 * i + 3]) for i in range(n)]
        p += 3 * n
    mcs = data[p]
    p += 1
    blocks = bytearray()
    while p < len(data) and data[p]:
        blocks += data[p + 1:p + 1 + data[p]]
        p += data[p] + 1
    clear, end = 1 << mcs, (1 << mcs) + 1
    out = bytearray()
    size = mcs + 1
    table = [bytes([i]) for i in range(clear)] + [b"", b""]
    prev = None
    bitpos, nbits = 0, len(blocks) * 8
    total = iw * ih
    while bitpos + size <= nbits and len(out) < total:
        code = 0
        for k in range(size):
            if blocks[(bitpos + k) >> 3] & (1 << ((bitpos + k) & 7)):
                code |= 1 << k
        bitpos += size
        if code == clear:
            table = table[:clear + 2]
            size = mcs + 1
            prev = None
            continue
        if code == end:
            break
        if code < len(table):
            entry = table[code]
            if prev is not None:
                table.append(prev + entry[:1])
        elif prev is not None:
            entry = prev + prev[:1]
            table.append(entry)
        else:
            break
        out += entry
        prev = entry
        if len(table) == (1 << size) and size < 12:
            size += 1
    out = bytes(out[:total])
    if iflags & 0x40 and len(out) == total:  # interlaced: rows come in 4 passes
        rows = []
        for start, step in ((0, 8), (4, 8), (2, 4), (1, 2)):
            rows += list(range(start, ih, step))
        fixed = bytearray(total)
        for k, y in enumerate(rows):
            fixed[y * iw:(y + 1) * iw] = out[k * iw:(k + 1) * iw]
        out = bytes(fixed)
    return iw, ih, pal, out


def colour_counts(colours):
    """colours: set of (r,g,b). Returns (distinct, after 15-bit 32X, after 9-bit Mega Drive)."""
    c15 = {(r >> 3, g >> 3, b >> 3) for r, g, b in colours}
    c9 = {(r >> 5, g >> 5, b >> 5) for r, g, b in colours}
    return len(colours), len(c15), len(c9)


def wav_info(data):
    if not data or data[:4] != b"RIFF":
        return None
    p, fmt, n = 12, None, 0
    while p + 8 <= len(data):
        tag, ln = data[p:p + 4], struct.unpack_from("<I", data, p + 4)[0]
        if tag == b"fmt ":
            ch, rate = struct.unpack_from("<HI", data, p + 10)
            bits = struct.unpack_from("<H", data, p + 22)[0]
            fmt = (rate, ch, bits)
        elif tag == b"data":
            n = ln
            break
        p += 8 + ln + (ln & 1)
    return fmt + (n,) if fmt else None


LAYER_TYPES = {0: "none", 1: "H", 2: "V", 3: "3Dfloor", 4: "3Dsky"}
MD_TILE_BUDGET = 1728  # 54 KB of the 64 KB VRAM: planes A and B (64x32), scroll table and sprite table take the rest


def read_layout(d, p, xs, ys):
    """xs*ys big-endian chunk numbers starting at p -> set of chunk numbers, next position."""
    chunks = set()
    for i in range(xs * ys):
        chunks.add(((d[p] << 8) | d[p + 1]) & 0x3FF)
        p += 2
    return chunks, p


def parse_backgrounds(d):
    """Backgrounds.bin -> list of (type, xs, ys, chunk set) for layers 1..n (engine: LoadStageBackground)."""
    layers = []
    if not d:
        return layers
    n, p = d[0], 1
    for _ in range(2):  # horizontal then vertical parallax entries, 4 bytes each
        cnt = d[p]
        p += 1 + 4 * cnt
    for _ in range(n):
        xs, ys, typ = d[p], d[p + 1], d[p + 2]
        p += 6
        while True:  # line scroll, RLE: 0xFF val count, ends with 0xFF 0xFF
            if d[p] == 0xFF:
                if d[p + 1] == 0xFF:
                    p += 2
                    break
                p += 3
            else:
                p += 1
        chunks, p = read_layout(d, p, xs, ys)
        layers.append((typ, xs, ys, chunks))
    return layers


def parse_act(d):
    """Act*.bin -> (active layer slots[4], fg (xs, ys, chunk set))."""
    n = d[0]
    p = 1 + n
    active = list(d[p:p + 4])
    xs, ys = d[p + 5], d[p + 6]
    chunks, _ = read_layout(d, p + 7, xs, ys)
    return active, (xs, ys, chunks)


def parse_chunks(d):
    """128x128Tiles.bin -> list of 512 chunks, each 64 entries (tile, direction, visualPlane)."""
    chunks = []
    for c in range(len(d) // 192):
        ents = []
        for t in range(64):
            e0, e1 = d[c * 192 + t * 3], d[c * 192 + t * 3 + 1]
            e0 &= 0x3F
            ents.append((e1 + ((e0 & 3) << 8), (e0 >> 2) & 3, e0 >> 4))
        chunks.append(ents)
    return chunks


def md_cells(pix, w, tiles):
    """16x16 tile numbers -> unique 8x8 cells (flips count as the same cell) and their colour sets."""
    seen, sets = set(), []
    transparent = pix[0] if pix else 0
    for t in tiles:
        for cy in (0, 8):
            for cx in (0, 8):
                rows = []
                for y in range(8):
                    o = (t * 16 + cy + y) * w + cx
                    rows.append(bytes(0 if v == transparent else v for v in pix[o:o + 8]))
                cell = b"".join(rows)
                variants = (cell, b"".join(r[::-1] for r in rows), b"".join(rows[::-1]), b"".join(r[::-1] for r in rows[::-1]))
                key = min(variants)
                if key not in seen:
                    seen.add(key)
                    sets.append(frozenset(cell) - {0})
    return len(seen), sets


def palette_lines(sets):
    """Greedy packing of cell colour sets into 15-colour lines (an upper bound, not optimal).
    Returns (lines, cells needing more than 15 colours)."""
    lines, over = [], 0
    for cs in sorted(set(sets), key=len, reverse=True):
        if len(cs) > 15:
            over += 1
            continue
        best = None
        for i, ln in enumerate(lines):
            if cs <= ln:
                best = (0, i)
                break
            grow = len(cs - ln)
            if len(ln) + grow <= 15 and (best is None or grow < best[0]):
                best = (grow, i)
        if best is None:
            lines.append(set(cs))
        else:
            lines[best[1]] |= cs
    return len(lines), over


def md_section(pk, by_folder, w):
    w("\n## Mega Drive planes and video memory (option 2: the VDP draws the layers)\n\n"
      "Per act, as the act starts (scripts can switch layers later): the layers drawn at once (slot: layer type; FG = foreground, BGn = background layer n, "
      "H = per-line horizontal scroll, V = vertical, 3D = special-stage floor/sky). Tiles = 16x16 tiles used by "
      "those layers; cells = unique 8x8 Mega Drive tiles after removing duplicates and H/V/HV flips "
      f"(budget about {MD_TILE_BUDGET} cells = 54 KB). Front = 16x16 tiles drawn in front of sprites "
      "(the Mega Drive's priority bit). Lines = 15-colour palette lines needed (greedy estimate; the "
      "Mega Drive has 4, shared with sprites); >15 = cells with too many colours for one line.\n\n"
      "| Act | Layers drawn | Objects | Tiles | Front tiles | Cells | KB | % of budget | Lines | >15 |\n"
      "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|\n")
    for folder in sorted(f for f in by_folder if f.startswith("Data/Stages/")):
        g = gif_decode(pk.read(folder + "/16x16Tiles.gif"))
        cd = pk.read(folder + "/128x128Tiles.bin")
        if not g or not cd:
            continue
        gw, gh, gpal, pix = g
        chunks = parse_chunks(cd)
        try:
            bgs = parse_backgrounds(pk.read(folder + "/Backgrounds.bin"))
        except IndexError:
            bgs = []
        for name, size in sorted(by_folder[folder]):
            base = name.rsplit("/", 1)[1]
            if not (base.lower().startswith("act") and base.lower().endswith(".bin")):
                continue
            d = pk.read(name)
            try:
                active, fg = parse_act(d)
            except IndexError:
                continue
            drawn, used_chunks = [], set()
            for slot in active:
                if slot == 0:
                    drawn.append("FG:H")
                    used_chunks |= fg[2]
                elif 1 <= slot <= len(bgs) and bgs[slot - 1][0] != 0:  # type 0 is not drawn
                    typ, xs, ys, ch = bgs[slot - 1]
                    drawn.append(f"BG{slot}:{LAYER_TYPES.get(typ, typ)}")
                    used_chunks |= ch
            # object count: after the type names (as in LoadActLayout)
            try:
                p = 1 + d[0] + 7 + fg[0] * fg[1] * 2
                ntypes = d[p]
                p += 1
                for _ in range(ntypes):
                    p += 1 + d[p]
                nobj = (d[p] << 8) | d[p + 1]
            except IndexError:
                nobj = -1
            tiles, front = set(), set()
            for c in used_chunks:
                if c < len(chunks):
                    for t, direction, plane in chunks[c]:
                        if t * 16 < gh:
                            tiles.add(t)
                            if plane:
                                front.add(t)
            ncells, sets = md_cells(pix, gw, sorted(tiles))
            nlines, over = palette_lines(sets)
            w(f"| {folder[12:]}/{base[:-4]} | {' '.join(drawn)} | {nobj} | {len(tiles)} | {len(front)} | {ncells} | "
              f"{ncells * 32 / 1024:.1f} | {ncells * 100 // MD_TILE_BUDGET} | {nlines} | {over} |\n")


def inventory(pk, out):
    KB = 1024.0
    by_folder = {}
    for name, off, size in pk.files:
        by_folder.setdefault(name.rsplit("/", 1)[0], []).append((name, size))
    w = out.write
    total = sum(s for _, _, s in pk.files)
    w(f"# Data.rsdk inventory\n\n{len(pk.files)} files, {total / KB / KB:.2f} MB in the pack.\n\n")

    w("## Size by top folder\n\n| Folder | Files | KB in pack |\n|---|---:|---:|\n")
    top = {}
    for name, off, size in pk.files:
        t = "/".join(name.split("/")[:2])
        c = top.setdefault(t, [0, 0])
        c[0] += 1
        c[1] += size
    for t in sorted(top):
        w(f"| {t} | {top[t][0]} | {top[t][1] / KB:.1f} |\n")

    w("\n## Stages\n\nRAM numbers are what the engine needs once a stage is loaded: tiles are "
      "16x16 pixels at 8 bits (the 16x16Tiles.gif height / 16 = tile count), layouts are in "
      "128-pixel chunks (2 bytes per chunk).\n\n"
      "| Stage folder | Tiles | Tile RAM KB | FG layout (chunks) | Layout files KB | Chunk table KB | Collision KB | Other KB |\n"
      "|---|---:|---:|---|---:|---:|---:|---:|\n")
    for folder in sorted(f for f in by_folder if f.startswith("Data/Stages/")):
        tiles = tile_ram = 0
        acts, layout_kb, chunk_kb, col_kb, other_kb = [], 0, 0, 0, 0
        for name, size in by_folder[folder]:
            base = name.rsplit("/", 1)[1].lower()
            if base == "16x16tiles.gif":
                gs = gif_size(pk.read(name))
                if gs:
                    tiles = gs[1] // 16
                    tile_ram = gs[0] * gs[1]
            elif base.startswith("act") and base.endswith(".bin"):
                d = pk.read(name)
                n = d[0]
                xs, ys = d[1 + n + 5], d[1 + n + 6]
                acts.append(f"{base[:-4]} {xs}x{ys}")
                layout_kb += size
            elif base == "backgrounds.bin":
                layout_kb += size
            elif base == "128x128tiles.bin":
                chunk_kb += size
            elif base == "collisionmasks.bin":
                col_kb += size
            else:
                other_kb += size
        w(f"| {folder[12:]} | {tiles} | {tile_ram / KB:.0f} | {', '.join(acts)} | {layout_kb / KB:.1f} | "
          f"{chunk_kb / KB:.1f} | {col_kb / KB:.1f} | {other_kb / KB:.1f} |\n")

    w("\n## Sprite sheets by folder\n\nRAM = width x height at 8 bits, the way the engine keeps every sheet in graphicData.\n\n"
      "| Folder | Sheets | RAM KB | Biggest sheet |\n|---|---:|---:|---|\n")
    for folder in sorted(f for f in by_folder if f.startswith("Data/Sprites")):
        sheets, ram, big = 0, 0, ("", 0, 0)
        for name, size in by_folder[folder]:
            if name.lower().endswith(".gif"):
                gs = gif_size(pk.read(name))
                if gs:
                    sheets += 1
                    ram += gs[0] * gs[1]
                    if gs[0] * gs[1] > big[1] * big[2]:
                        big = (name.rsplit("/", 1)[1], gs[0], gs[1])
        if sheets:
            w(f"| {folder[13:] or '(top)'} | {sheets} | {ram / KB:.0f} | {big[0]} {big[1]}x{big[2]} |\n")

    # ---- colours actually used
    master = pk.read("Data/Palettes/MasterPalette.act") or b""
    master_pal = [tuple(master[3 * i:3 * i + 3]) for i in range(len(master) // 3)]
    w("\n## Colours actually used\n\nA stage's palette is MasterPalette.act (0-95) + StageConfig.bin (96-127) + "
      "the tileset GIF (128-255). Tiles: colours the 16x16 tile pixels really use (the first pixel's index is the transparent colour, as in the engine). "
      "Reduced to the 32X's 15-bit and the Mega Drive's 9-bit colour. "
      "The Mega Drive shows at most 61 colours at once (4 lines x 15 + backdrop), the 32X 256.\n\n"
      "| Stage folder | Palette entries set | Tile colours | as 32X 15-bit | as Mega Drive 9-bit |\n|---|---:|---:|---:|---:|\n")
    for folder in sorted(f for f in by_folder if f.startswith("Data/Stages/")):
        pal = list(master_pal) + [(0, 0, 0)] * (256 - len(master_pal))
        sc = pk.read(folder + "/StageConfig.bin")
        if sc and len(sc) >= 1 + 96:
            for i in range(32):
                pal[96 + i] = tuple(sc[1 + 3 * i:4 + 3 * i])
        g = gif_decode(pk.read(folder + "/16x16Tiles.gif"))
        if not g:
            continue
        gw, gh, gpal, pix = g
        if len(gpal) == 256:
            for i in range(128, 256):
                pal[i] = gpal[i]
        used = set(pix)
        used.discard(pix[0] if pix else 0)  # the engine makes the first pixel's index transparent
        cols = {pal[i] for i in used}
        n, n15, n9 = colour_counts(cols)
        w(f"| {folder[12:]} | {len(set(pal))} | {n} | {n15} | {n9} |\n")

    w("\nSprites: colours used per sprite folder, through the GIF's own palette (the engine maps sheet "
      "indices onto the active palette, so this is an upper bound of what the sheets ask for).\n\n"
      "| Sprite folder | Colours used | as 32X 15-bit | as Mega Drive 9-bit |\n|---|---:|---:|---:|\n")
    for folder in sorted(f for f in by_folder if f.startswith("Data/Sprites")):
        cols = set()
        for name, size in by_folder[folder]:
            if name.lower().endswith(".gif"):
                g = gif_decode(pk.read(name))
                if g:
                    gw, gh, gpal, pix = g
                    for i in set(pix) - {0}:
                        if i < len(gpal):
                            cols.add(gpal[i])
        if cols:
            n, n15, n9 = colour_counts(cols)
            w(f"| {folder[13:] or '(top)'} | {n} | {n15} | {n9} |\n")

    md_section(pk, by_folder, w)

    w("\n## Music\n\n| File | KB |\n|---|---:|\n")
    for name, off, size in pk.files:
        if name.startswith("Data/Music/"):
            w(f"| {name[11:]} | {size / KB:.0f} |\n")

    w("\n## Sound effects\n\n| Folder | Files | 16-bit RAM KB | Formats seen |\n|---|---:|---:|---|\n")
    sfx = {}
    for name, off, size in pk.files:
        if name.startswith("Data/SoundFX/") and name.lower().endswith(".wav"):
            folder = name[13:].rsplit("/", 1)[0] if "/" in name[13:] else "(top)"
            wi = wav_info(pk.read(name))
            c = sfx.setdefault(folder, [0, 0, set()])
            c[0] += 1
            if wi:
                rate, ch, bits, n = wi
                c[1] += n * (2 if bits == 8 else 1)
                c[2].add(f"{rate} Hz {ch}ch {bits}bit")
    for folder in sorted(sfx):
        c = sfx[folder]
        w(f"| {folder} | {c[0]} | {c[1] / KB:.0f} | {', '.join(sorted(c[2]))} |\n")

    w("\n## Scripts\n\n| Folder | Files | KB |\n|---|---:|---:|\n")
    sc = {}
    for name, off, size in pk.files:
        if name.startswith("Data/Scripts/"):
            folder = name.rsplit("/", 1)[0][13:]
            c = sc.setdefault(folder, [0, 0])
            c[0] += 1
            c[1] += size
    for folder in sorted(sc):
        w(f"| {folder} | {sc[folder][0]} | {sc[folder][1] / KB:.1f} |\n")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    pk = Pack(path)
    if cmd == "list":
        for name, off, size in pk.files:
            print(f"{size:10d}  {name}")
    elif cmd == "extract":
        outdir = sys.argv[3]
        for name, off, size in pk.files:
            dst = os.path.join(outdir, name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(decrypt(pk.raw[off:off + size]))
        print(f"{len(pk.files)} files extracted to {outdir}")
    elif cmd == "inventory":
        out = open(sys.argv[3], "w") if len(sys.argv) > 3 else sys.stdout
        inventory(pk, out)
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
