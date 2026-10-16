#!/usr/bin/env python3
"""PORT32X: round S2 measurements for the option-2 design (the Mega Drive VDP draws the
planes and the original-style sprites, the SH2s run the engine, the 32X draws the rest).

  tools/scd_report.py Data.rsdk report.md

Sections:
  1. Palette lines: each act's tiles and its sprites packed into the Mega Drive's four
     15-colour lines (optimised, with a lower bound), and which sprites do not fit.
  2. Music: exact duration of every track, totals, and the CD time they need.
  3. Sound effects: what each stage really loads (GameConfig + StageConfig), at 22 kHz.
  4. Scripts: the bytecode each stage loads and its size once expanded by the engine.
  5. Cart budget per act (all four time periods) for the 4 MB cart.
Built on tools/rsdk_pack.py (the pack reader checked against the engine)."""
import os, random, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk_pack as rp

KB = 1024.0
LINE = 15  # colours per Mega Drive palette line (index 0 is transparent)


# ------------------------------------------------------------------ fast partial decrypt
def _states(size):
    no = (size & 0x1FC) >> 2
    pb = (no % 9) + 1
    pa = (no % pb) + 1
    sw = 0
    while True:
        yield pa, pb, no, sw
        pa += 1
        pb += 1
        if pa <= 19 or pb <= 11:
            if pa > 19:
                pa = 1
                sw ^= 1
            if pb > 11:
                pb = 1
                sw ^= 1
        else:
            no = (no + 1) & 0x7F
            if sw:
                sw = 0
                pa = (no % 12) + 6
                pb = (no % 5) + 4
            else:
                sw = 1
                pa = (no % 15) + 3
                pb = (no % 7) + 1


_cycles = {}


def _cycle(size):
    """The key stream's states: a short lead-in, then a cycle (13,176 long in practice)."""
    key = (size & 0x1FC) >> 2  # the start state only depends on this
    if key not in _cycles:
        seen, seq = {}, []
        for i, s in enumerate(_states(size)):
            if s in seen:
                _cycles[key] = (seq, seen[s])
                break
            seen[s] = i
            seq.append(s)
    return _cycles[key]


def decrypt_range(raw, size, start, end):
    """Decrypt bytes [start, end) of a file whose encrypted data is raw (len size)."""
    seq, c0 = _cycle(size)
    L = len(seq) - c0
    out = bytearray()
    for k in range(max(0, start), min(end, size)):
        pa, pb, no, sw = seq[k] if k < len(seq) else seq[c0 + (k - c0) % L]
        x = raw[k] ^ rp.KEY_B[pb] ^ no
        if sw:
            x = ((x & 0xF) << 4) | (x >> 4)
        out.append((x ^ rp.KEY_A[pa]) & 0xFF)
    return bytes(out)


class Data:
    def __init__(self, path):
        self.pk = rp.Pack(path)
        self.index = {n.lower(): (n, o, s) for n, o, s in self.pk.files}
        self.cache = {}

    def has(self, name):
        return name.lower() in self.index

    def size(self, name):
        e = self.index.get(name.lower())
        return e[2] if e else 0

    def read(self, name):
        k = name.lower()
        if k not in self.cache:
            self.cache[k] = self.pk.read(name)
        return self.cache[k]

    def read_range(self, name, start, end):
        n, off, size = self.index[name.lower()]
        return decrypt_range(self.pk.raw[off:off + size], size, start, end)

    def names(self, prefix):
        p = prefix.lower()
        return [n for n, o, s in self.pk.files if n.lower().startswith(p)]


# ------------------------------------------------------------------ game / stage config
def _str(d, p):
    n = d[p]
    return d[p + 1:p + 1 + n].decode("latin-1"), p + 1 + n


def parse_gameconfig(d):
    """-> dict(sfx=[names], lists={cat: [(folder, id, name)]}) as RetroEngine::LoadGameConfig."""
    p = 0
    for _ in range(3):
        _, p = _str(d, p)
    nobj = d[p]
    p += 1
    for _ in range(2 * nobj):  # object names, then script paths
        _, p = _str(d, p)
    nvar = d[p]
    p += 1
    for _ in range(nvar):
        _, p = _str(d, p)
        p += 4
    nsfx = d[p]
    p += 1
    sfx = []
    for _ in range(nsfx):
        s, p = _str(d, p)
        sfx.append(s)
    nplr = d[p]
    p += 1
    for _ in range(nplr):
        _, p = _str(d, p)
    lists = {}
    for c in range(4):
        cat = {2: 3, 3: 2}.get(c, c)  # special and bonus are swapped in the file
        n = d[p]
        p += 1
        entries = []
        for _ in range(n):
            folder, p = _str(d, p)
            sid, p = _str(d, p)
            name, p = _str(d, p)
            p += 1
            entries.append((folder, sid, name))
        lists[cat] = entries
    return {"sfx": sfx, "lists": lists}


def parse_stageconfig(d):
    """-> (stage palette 96-127, object count, sfx names) as LoadStageFiles."""
    pal = [tuple(d[1 + 3 * i:4 + 3 * i]) for i in range(32)]
    p = 1 + 96
    nobj = d[p]
    p += 1
    for _ in range(2 * nobj):  # object names, then script paths
        _, p = _str(d, p)
    nsfx = d[p]
    p += 1
    sfx = []
    for _ in range(nsfx):
        s, p = _str(d, p)
        sfx.append(s)
    return pal, nobj, sfx


def bytecode_counts(d):
    """-> (script code ints, jump table ints) as LoadBytecode."""
    p = 0

    def block(p):
        (count,) = struct.unpack_from("<i", d, p)
        p += 4
        total = count
        while count > 0:
            b = d[p]
            p += 1
            n = b & 0x7F
            p += n * (4 if b >= 0x80 else 1)
            count -= n
        return total, p

    code, p = block(p)
    jumps, p = block(p)
    return code, jumps


# ------------------------------------------------------------------ palette lines
def pack_lines(sets, cap=LINE, tries=200, seed=1):
    """Pack colour sets into as few lines (bins of cap colours) as possible, each set
    whole in one line. Best of several randomised first-fit-decreasing runs, each followed
    by an attempt to empty its smallest lines. Returns (lines [set], lower bound)."""
    sets = [s for s in set(frozenset(x) for x in sets) if s]
    sets = [s for s in sets if not any(s < t for t in sets)]
    if not sets:
        return [], 0
    if any(len(s) > cap for s in sets):
        raise ValueError("a set is bigger than a line")
    union = frozenset().union(*sets)
    lower = -(-len(union) // cap)
    rnd = random.Random(seed)
    best = None
    for t in range(tries):
        order = sorted(sets, key=lambda s: (-len(s), rnd.random() if t else 0))
        lines, members = [], []
        for s in order:
            pick, score = None, None
            for i, ln in enumerate(lines):
                if len(ln | s) <= cap:
                    # two placement rules, alternating between tries: most shared colours,
                    # or least growth (the old greedy's rule)
                    sc = (len(ln & s), -len(ln | s)) if t % 2 == 0 else (-len(s - ln), -len(ln))
                    if score is None or sc > score:
                        pick, score = i, sc
            if pick is None:
                lines.append(set(s))
                members.append([s])
            else:
                lines[pick] |= s
                members[pick].append(s)
        improved = True
        while improved and len(lines) > lower:
            improved = False
            for i in sorted(range(len(lines)), key=lambda i: len(lines[i])):
                trial = [set(l) for l in lines]
                tmem = [list(m) for m in members]
                ok = True
                for s in members[i]:
                    placed = False
                    for j in range(len(trial)):
                        if j != i and len(trial[j] | s) <= cap:
                            trial[j] |= s
                            tmem[j].append(s)
                            placed = True
                            break
                    if not placed:
                        ok = False
                        break
                if ok:
                    lines = [l for j, l in enumerate(trial) if j != i]
                    members = [m for j, m in enumerate(tmem) if j != i]
                    improved = True
                    break
        if best is None or len(lines) < len(best):
            best = lines
            if len(best) == lower:
                break
    return best, lower


def add_sprites(lines, sprite_sets, maxlines=4, cap=LINE):
    """Fit sprite colour sets into the tile lines (and new lines up to maxlines).
    -> (lines, sets that did not fit)."""
    lines = [set(l) for l in lines]
    left = []
    for s in sorted(set(sprite_sets), key=len, reverse=True):
        if not s:
            continue
        pick, score = None, None
        for i, ln in enumerate(lines):
            if len(ln | s) <= cap:
                sc = (len(ln & s), -len(ln | s))
                if score is None or sc > score:
                    pick, score = i, sc
        if pick is not None:
            lines[pick] |= s
        elif len(lines) < maxlines and len(s) <= cap:
            lines.append(set(s))
        else:
            left.append(s)
    return lines, left


def md9(c):
    return (c[0] >> 5, c[1] >> 5, c[2] >> 5)


def sprite_components(pix, w, h):
    """Connected areas of non-transparent pixels (8-neighbours) = sprite frames, roughly.
    -> list of (index set, bounding-box area)."""
    parent = list(range(w * h))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for y in range(h):
        row = y * w
        for x in range(w):
            i = row + x
            if not pix[i]:
                continue
            for dx, dy in ((-1, 0), (-1, -1), (0, -1), (1, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and ny >= 0 and pix[ny * w + nx]:
                    ra, rb = find(i), find(ny * w + nx)
                    if ra != rb:
                        parent[ra] = rb
    comps = {}
    for y in range(h):
        for x in range(w):
            i = y * w + x
            v = pix[i]
            if v:
                r = find(i)
                c = comps.get(r)
                if c is None:
                    comps[r] = [set([v]), x, y, x, y]
                else:
                    c[0].add(v)
                    if x < c[1]:
                        c[1] = x
                    if x > c[3]:
                        c[3] = x
                    if y < c[2]:
                        c[2] = y
                    if y > c[4]:
                        c[4] = y
    return [(frozenset(c[0]), (c[3] - c[1] + 1) * (c[4] - c[2] + 1)) for c in comps.values()]


# ------------------------------------------------------------------ ogg
def ogg_duration(data, name):
    head = data.read_range(name, 0, 4096)
    i = head.find(b"\x01vorbis")
    if i < 0:
        return None
    rate = struct.unpack_from("<I", head, i + 12)[0]
    size = data.size(name)
    tail = data.read_range(name, max(0, size - 65536), size)
    j = tail.rfind(b"OggS")
    if j < 0 or not rate:
        return None
    gran = struct.unpack_from("<q", tail, j + 6)[0]
    return gran / rate


def wav_samples(data, name):
    wi = rp.wav_info(data.read(name)) if data.has(name) else None
    if not wi:
        return 0, None
    rate, ch, bits, n = wi
    return n // (bits // 8) // max(ch, 1), wi


# ------------------------------------------------------------------ report
def report(path, out):
    w = out.write
    data = Data(path)
    gc = parse_gameconfig(data.read("Data/Game/GameConfig.bin"))
    master = data.read("Data/Palettes/MasterPalette.act") or b""
    master_pal = [tuple(master[3 * i:3 * i + 3]) for i in range(len(master) // 3)]
    mobile = data.has("Data/Scripts/ByteCode/GlobalCode.bin")
    w(f"# SonicCD 32X – round S2 report\n\nPack: {len(data.pk.files)} files. Bytecode: "
      f"{'mobile (GlobalCode.bin + <folder>.bin)' if mobile else 'PC (GS000.bin + RSnnn.bin)'}.\n")

    # ---- stage list -> stages
    stages = []  # (cat, pos, folder, act id)
    for cat in (0, 1, 2, 3):
        for pos, (folder, sid, name) in enumerate(gc["lists"].get(cat, [])):
            stages.append((cat, pos, folder, sid))

    def bytecode_name(cat, pos, folder):
        if mobile:
            return f"Data/Scripts/ByteCode/{folder}.bin"
        return f"Data/Scripts/ByteCode/{'PRBS'[cat]}S{pos:03d}.bin"

    # ---- per stage: palette, sfx, scripts, MD cells
    sheet_cache = {}

    def sheets_for(folder):
        if folder.startswith("SS"):
            return []
        zone = "R" + folder[1] if folder.startswith("R") and len(folder) > 1 else None
        names = data.names("Data/Sprites/Global/") + data.names("Data/Sprites/Players/")
        if zone:
            names += data.names(f"Data/Sprites/{zone}/")
        return [n for n in names if n.lower().endswith(".gif")]

    def sheet_comps(name):
        if name not in sheet_cache:
            g = rp.gif_decode(data.read(name))
            sheet_cache[name] = sprite_components(g[3], g[0], g[1]) if g else []
            sys.stderr.write(".")
            sys.stderr.flush()
        return sheet_cache[name]

    rows = []
    sfx_global = gc["sfx"]
    for cat, pos, folder, sid in stages:
        base = f"Data/Stages/{folder}/"
        if not data.has(base + "StageConfig.bin") or not data.has(base + "16x16Tiles.gif"):
            continue
        spal, nobj, ssfx = parse_stageconfig(data.read(base + "StageConfig.bin"))
        pal = list(master_pal) + [(0, 0, 0)] * (256 - len(master_pal))
        pal[96:128] = spal
        g = rp.gif_decode(data.read(base + "16x16Tiles.gif"))
        gw, gh, gpal, pix = g
        if len(gpal) == 256:
            pal[128:256] = gpal[128:256]
        act = f"{base}Act{sid}.bin"
        cells, cell_sets, ntiles = 0, [], 0
        if data.has(act) and data.has(base + "128x128Tiles.bin"):
            try:
                active, fg = rp.parse_act(data.read(act))
                bgs = rp.parse_backgrounds(data.read(base + "Backgrounds.bin")) if data.has(base + "Backgrounds.bin") else []
                chunks = rp.parse_chunks(data.read(base + "128x128Tiles.bin"))
                used = set()
                for slot in active:
                    if slot == 0:
                        used |= fg[2]
                    elif 1 <= slot <= len(bgs) and bgs[slot - 1][0] != 0:
                        used |= bgs[slot - 1][3]
                tiles = {t for c in used if c < len(chunks) for t, d_, p_ in chunks[c] if t * 16 < gh}
                ntiles = len(tiles)
                cells, cell_sets = rp.md_cells(pix, gw, sorted(tiles))
            except (IndexError, struct.error):
                pass
        tile_cols = [frozenset(md9(pal[i]) for i in s) for s in cell_sets]
        spr = []
        for sh in sheets_for(folder):
            for idx, area in sheet_comps(sh):
                spr.append((frozenset(md9(pal[i]) for i in idx), area, sh))
        bc = bytecode_name(cat, pos, folder)
        code = jumps = 0
        if data.has(bc):
            code, jumps = bytecode_counts(data.read(bc))
        rows.append(dict(cat=cat, pos=pos, folder=folder, sid=sid, nobj=nobj, sfx=ssfx, cells=cells,
                         ntiles=ntiles, tile_cols=tile_cols, spr=spr, bc=bc, code=code, jumps=jumps,
                         layout=data.size(act) + data.size(base + "Backgrounds.bin"),
                         chunk=data.size(base + "128x128Tiles.bin")))
    sys.stderr.write("\n")

    # ---- 1. palette lines
    w("\n## 1. Palette lines (Mega Drive: 4 lines of 15 colours, shared by planes and sprites)\n\n"
      "Colours reduced to the Mega Drive's 9 bits. Tiles = the act's unique 8x8 cells. Sprites = connected areas "
      "of the Global, Players and zone sprite sheets (about one sprite frame each), coloured through this "
      "stage's palette. Tile lines = optimised packing (best of 200 tries) with its lower bound; "
      "+ sprites = lines after adding the sprites while keeping 4 lines; Left over = sprite areas that "
      "do not fit (candidates for the 32X layer or for palette work), with their sheets.\n\n"
      "| Stage | Tile lines | Lower bound | Lines with sprites | Left over | Sheets of the left-overs |\n"
      "|---|---:|---:|---:|---:|---|\n")
    for r in rows:
        if not r["tile_cols"]:
            continue
        try:
            lines, lower = pack_lines(r["tile_cols"])
        except ValueError:
            w(f"| {r['folder']}/Act{r['sid']} | >15 in a cell | | | | |\n")
            continue
        spr_sets = [s for s, a, sh in r["spr"] if len(s) <= LINE]
        too_big = [sh for s, a, sh in r["spr"] if len(s) > LINE]
        lines2, left = add_sprites(lines, spr_sets)
        left_sheets = sorted({sh.rsplit("/", 2)[-2] + "/" + sh.rsplit("/", 1)[1]
                              for s, a, sh in r["spr"] if s in set(left)} | {sh.rsplit("/", 2)[-2] + "/" + sh.rsplit("/", 1)[1] for sh in too_big})
        r["lines"] = len(lines)
        w(f"| {r['folder']}/Act{r['sid']} | {len(lines)} | {lower} | {len(lines2)} | {len(left) + len(too_big)} | "
          f"{', '.join(left_sheets[:6])}{' …' if len(left_sheets) > 6 else ''} |\n")

    # ---- 2. music
    w("\n## 2. Music (CD audio tracks)\n\n| Track | Seconds |\n|---|---:|\n")
    groups = {}
    for name in sorted(data.names("Data/Music/")):
        if not name.lower().endswith(".ogg"):
            continue
        sec = ogg_duration(data, name)
        rel = name[11:]
        grp = rel.split("/")[0] if "/" in rel else "shared"
        g = groups.setdefault(grp, [0, 0.0])
        g[0] += 1
        g[1] += sec or 0
        w(f"| {rel} | {sec:.1f} |\n" if sec else f"| {rel} | ? |\n")
    tracks = sum(g[0] for g in groups.values())
    music = sum(g[1] for g in groups.values())
    w("\n| Set | Tracks | Minutes |\n|---|---:|---:|\n")
    for grp in sorted(groups):
        w(f"| {grp} | {groups[grp][0]} | {groups[grp][1] / 60:.1f} |\n")
    gaps = tracks * 2.0
    w(f"\nAll music: {tracks} tracks, {music / 60:.1f} min, plus 2 s gap per track ({gaps / 60:.1f} min). "
      f"A CD holds 74 min (80 min on 700 MB discs) and at most 99 tracks; the data track takes "
      f"about 1 min per 9 MB (150 KB/s).\n")

    # ---- 3. sound effects
    w("\n## 3. Sound effects\n\nSizes at 22,050 Hz 8-bit (the slave SH2's PWM mixer, read from the cart). "
      "The data is 44,100 Hz 8-bit mono.\n\n")
    gsz = 0
    for n in sfx_global:
        s, wi = wav_samples(data, "Data/SoundFX/" + n)
        gsz += s // 2
    w(f"Global sound effects (GameConfig, always loaded): {len(sfx_global)} files, {gsz / KB:.0f} KB at 22 kHz.\n\n"
      "| Stage | Stage sfx | KB at 22 kHz | With global KB |\n|---|---:|---:|---:|\n")
    sfx_bytes = {}
    for r in rows:
        b = 0
        for n in r["sfx"]:
            s, wi = wav_samples(data, "Data/SoundFX/" + n)
            b += s // 2
        r["sfx_kb"] = b / KB
        w(f"| {r['folder']}/Act{r['sid']} | {len(r['sfx'])} | {b / KB:.0f} | {(b + gsz) / KB:.0f} |\n")
    w("\nBiggest sound effects (seconds at their own rate):\n\n| File | Seconds |\n|---|---:|\n")
    allsfx = []
    for n in data.names("Data/SoundFX/"):
        s, wi = wav_samples(data, n)
        if wi:
            allsfx.append((s / wi[0], n[13:]))
    for sec, n in sorted(allsfx, reverse=True)[:12]:
        w(f"| {n} | {sec:.2f} |\n")

    # ---- 4. scripts
    gname = "Data/Scripts/ByteCode/GlobalCode.bin" if mobile else "Data/Scripts/ByteCode/GS000.bin"
    gcode, gjump = bytecode_counts(data.read(gname)) if data.has(gname) else (0, 0)
    gkb = (gcode + gjump) * 4 / KB
    w(f"\n## 4. Scripts\n\nThe engine expands bytecode to 4-byte ints (scriptCode + jumpTable). "
      f"Global scripts: {gcode} + {gjump} ints = {gkb:.0f} KB. The engine's arrays hold 256K + 16K ints "
      f"(1,088 KB).\n\n| Stage | Bytecode file | Ints | KB | With global KB |\n|---|---|---:|---:|---:|\n")
    for r in rows:
        kb = (r["code"] + r["jumps"]) * 4 / KB
        r["script_kb"] = kb
        w(f"| {r['folder']}/Act{r['sid']} | {r['bc'].rsplit('/', 1)[1]} | {r['code'] + r['jumps']} | {kb:.0f} | {kb + gkb:.0f} |\n")

    # ---- 5. cart budget per act (time periods A-D together)
    COLL = 2 * (4 * 0x4000 + 0x1000 + 0x400) / KB
    w("\n## 5. Cart budget per act, all time periods loaded together (4 MB = 4,096 KB)\n\n"
      f"Per period: Mega Drive tiles (cells x 32 bytes), chunk table, collision in the engine's form "
      f"({COLL:.0f} KB), layouts, stage scripts. Shared: sprites at 4 bits (bounding boxes of the sprite "
      "areas of Global + Players + zone), global + stage sound effects at 22 kHz, global scripts.\n\n"
      "| Act | Periods | Per-period KB (sum) | Sprites KB | Sfx KB | Global scripts KB | Total KB | % of cart |\n"
      "|---|---|---:|---:|---:|---:|---:|---:|\n")
    acts = {}
    for r in rows:
        if r["folder"].startswith("R") and len(r["folder"]) == 4:
            acts.setdefault(r["folder"][:3], []).append(r)
    for key in sorted(acts):
        rs = acts[key]
        per = sum(r["cells"] * 32 / KB + r["chunk"] / KB + COLL + r["layout"] / KB + r["script_kb"] for r in rs)
        seen, spr = set(), 0
        for s, a, sh in rs[0]["spr"]:
            spr += a / 2
        sfx_union = set()
        for r in rs:
            sfx_union |= set(r["sfx"])
        sb = gsz
        for n in sfx_union:
            s, wi = wav_samples(data, "Data/SoundFX/" + n)
            sb += s // 2
        total = per + spr / KB + sb / KB + gkb
        w(f"| {key} | {''.join(r['folder'][3] for r in rs)} | {per:.0f} | {spr / KB:.0f} | {sb / KB:.0f} | {gkb:.0f} | "
          f"{total:.0f} | {total * 100 / 4096:.0f} |\n")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    with open(sys.argv[2], "w") as out:
        report(sys.argv[1], out)
    print(f"report written to {sys.argv[2]}")


if __name__ == "__main__":
    main()
