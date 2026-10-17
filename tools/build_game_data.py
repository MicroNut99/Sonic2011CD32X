#!/usr/bin/env python3
"""PORT32X S7d: the game's data for the engine disc.
  tools/build_game_data.py Data.rsdk [out dir, default game_data] [--scenes 1:*,1:*@1] [--start 1:0] [-j cores]
--scenes: which scenes go on the disc - "list:pos" or "list:*" (default 1:0); "list:pos@1" = that scene played
          as Tails (REV R3): its image is L<list>S<pos + 128>.IMG; "-list:pos" leaves one out again (REV R4: as
          Sonic and as Tails; "-list:pos@1" as Tails only), e.g. 1:*,1:*@1,-1:69
--start:  the scene the engine's cart holds at power-on (default 1:0, Palmtree Panic 1 as Sonic): its image is
          written as L0S000.IMG, the file the boot loader loads (R1Q); scene 0:0, if included, takes its number
Output (cd32x/Makefile ENGINE=1 puts it on the disc):
  L<list>S<pos>.IMG   every scene's console cart image in the FIXED layout: each part at the same
                      offset in every image (sized to the largest INCLUDED scene), because the
                      engine binds those addresses once at start-up; at each scene change the
                      engine has the Sub-CPU load the scene's file into cart 0x080000 (max 2.5 MB).
                      REV R4: part 17 is the scene's own data pack (the files the SH2 reads in it -
                      the engine reads files from the image in the cart, so no shared pack has to
                      hold every scene's files), and the header carries a CRC of every 32 KB chunk
                      (after the part table, "SCHK"): a scene change loads only the chunks whose
                      CRCs differ from the image already in the cart
  SCDDATA.PAK         the start scene's pack (the boot loader loads a data pack; the engine uses the
                      one in the scene image) - cart 0x300000. REV R4B: then the loading picture (IMAGE.RAW's
                      bytes, at a multiple of 4) and a 16-byte footer: 'SPL1', its offset, its size, 0 - the
                      engine shows it between the scenes (cd32x/engine/plat_mars.cpp)
  IMAGE.RAW           REV R4B: the loading picture (tools/scd_splash.py: the title screen with a loading bar) -
                      file 0: the boot loader shows it at power-on (not copied by the Makefile: the build
                      commands copy it into the disc folder, as KOBOSFX.BIN)
  --native DIR        REV R4: part 18 of each image: the scene's own scripts as native SH2 code (tools/scd_aot.py
                      --part stage), compiled and linked for part 18's place in the cart against the engine in
                      DIR (cd32x/build_engine: engine.elf, engine.bin - build the engine first: the blobs are for
                      that engine only; engine/ScriptNative.ops from tools/build_native.sh). Without it the
                      part is empty and only the global scripts run native.
  G0S255.MDG          REV R4: the dev menu's font as an act file (md/sgb.h "SCMG"): the game's own 8 x 8
                      font (SystemText.gif) as Genesis cells - character c is cell 1 + c, highlighted
                      257 + c; the SH2 writes the menu's text into plane A (cd32x/engine/plat_mars.cpp)
  game_data.md        what is in it, and checks: CRCs of every fixed image, sizes"""
import atexit, os, re, shutil, struct, subprocess, sys, tempfile, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk_pack as rp
import scd_report as sr
import measure_stages as ms
import scd_splash

ROOT = ms.ROOT
SCENE_MAX = 0x300000 - 0x080000  # S7e: the scene image now carries sprite and tile pixels
DATA_MAX = 0x380000 - 0x300000   # the act file's cart slot starts at 0x380000
CHUNK = 32768                    # the Sub-CPU's load unit (CMD 46)
HASH_MAX = 96                    # chunk CRCs the header has room for (3 MB; the scene slot is 2.5 MB)
# REV R4: files every scene's pack carries whole - the dev menu reads them (its stage names from GameConfig.bin's
# stage lists, its font's header; the stage runs read only the start of some of them)
MENU_FILES = ["Data/Game/GameConfig.bin", "Data/Game/SystemText.gif", "Data/Palettes/MasterPalette.act"]


def read_image(path):
    d = open(path, "rb").read()
    assert d[:4] == b"SCCB" and struct.unpack(">I", d[4:8])[0] == 2, path
    n = struct.unpack(">I", d[8:12])[0]
    parts = []
    for k in range(n):
        off, size, es, cnt, crc = struct.unpack(">5I", d[12 + 20 * k:32 + 20 * k])
        parts.append((d[off:off + size], es, cnt, crc))
    return parts


def chunk_hashes(img):
    """REV R4: per 32 KB chunk two checksums (CRC-32, Adler-32); chunk 0 (this header) is always loaded: 0, 0"""
    out = [(0, 0)]
    for c in range(1, (len(img) + CHUNK - 1) // CHUNK):
        d = img[c * CHUNK:(c + 1) * CHUNK]
        out.append((zlib.crc32(d) & 0xFFFFFFFF, zlib.adler32(d) & 0xFFFFFFFF))
    return out


# REV R4C: the parts' order in the image - the 32X picture mode's pixels (14 the sprite sheets, 15 the tiles) last: only
# its scenes (the presentation and special stage lists) carry them, so every other image ends before them (their room
# is reserved in the layout, not in the files)
PM_LISTS = (0, 3)
TILESET_SIZE = 0x40000
def part_order(n):
    return [k for k in list(range(14)) + [16, 17, 18, 15, 14] if k < n] + list(range(19, n))


def part_offsets(n, caps):
    """REV R4: where each of the n parts starts in a fixed-layout image (caps: the parts' room), in part_order"""
    pos = (12 + 20 * n + 8 + 8 * HASH_MAX + 15) & ~15
    out = [0] * n
    for k in part_order(n):
        out[k] = pos
        pos += (caps[k] + 15) & ~15 if k < len(caps) else 0
    return out


def fixed_image(parts, caps):
    n = len(parts)
    hdr = 12 + 20 * n
    out = bytearray(hdr + 8 + 8 * HASH_MAX)
    out[0:4] = b"SCCB"
    out[4:8] = struct.pack(">I", 2)
    out[8:12] = struct.pack(">I", n)
    offs = part_offsets(n, caps)
    end = len(out)
    for k, (data, es, cnt, crc) in enumerate(parts):
        assert len(data) <= caps[k]
        out[12 + 20 * k:32 + 20 * k] = struct.pack(">5I", offs[k], len(data), es, cnt, crc)
        if data:  # REV R4C: the file ends after the last part with data (an empty part's room is not in the file)
            end = max(end, offs[k] + ((caps[k] + 15) & ~15 if k == 15 else len(data)))
    end = (end + 15) & ~15
    out.extend(b"\0" * (end - len(out)))
    for k, (data, es, cnt, crc) in enumerate(parts):
        if data:
            out[offs[k]:offs[k] + len(data)] = data
    hs = chunk_hashes(bytes(out))
    assert len(hs) <= HASH_MAX, f"{len(hs)} chunks: more than the header's {HASH_MAX}"
    out[hdr:hdr + 8] = b"SCHK" + struct.pack(">I", len(hs))
    for c, (a, b) in enumerate(hs):
        out[hdr + 8 + 8 * c:hdr + 16 + 8 * c] = struct.pack(">II", a, b)
    return bytes(out), hdr


def md_colour(rgb):
    """(r, g, b) 8 bits -> Genesis CRAM word, as md/sgb.c sgb_md_color (via RGB565: the top 3 bits a channel)"""
    r, g, b = (c >> 5 for c in rgb)
    return b << 9 | g << 5 | r << 1


def menu_mdg(src):
    """REV R4: G0S255.MDG - the dev menu's font (md/sgb.h): header, 513 cells (0 blank; 1 + c normal, 257 + c
    highlighted - SystemText.gif's two halves, as the engine's DrawTextMenuEntry reads them), the palette slots, CRAM,
    the zero block command 68 clears the planes from. No planes, no sprites: the SH2 writes plane A itself."""
    w, h, _, px = rp.gif_decode(src.read("Data/Game/SystemText.gif"))
    master = src.read("Data/Palettes/MasterPalette.act")
    assert w == 128 and h >= 256, (w, h)
    used = sorted({v for v in px} - {0})
    assert len(used) <= 15, used
    slot = {v: k + 1 for k, v in enumerate(used)}  # engine palette index -> CRAM slot (0: see-through)
    cells = bytearray(32)  # cell 0: blank
    for half in (0, 128):
        for c in range(256):
            x0, y0 = (c & 15) * 8, (c >> 4) * 8 + half
            for y in range(8):
                row = 0
                for x in range(8):
                    row = row << 4 | slot.get(px[(y0 + y) * w + x0 + x], 0)
                cells += struct.pack(">I", row)
    ncells = len(cells) // 32
    slots = [0xFFFF] * 64
    cram = [0] * 64
    # REV R4B: the highlighted half's letters yellow (the font's own: white, next to the others' grey - too close on a TV)
    hi = {v for v in px[128 * w:256 * w]} - {v for v in px[:128 * w]}
    for v, k in slot.items():
        slots[k] = v
        cram[k] = md_colour(tuple(master[3 * v:3 * v + 3])) if master and len(master) >= 3 * v + 3 else 0x0EEE
        if v in hi:
            cram[k] = md_colour((255, 255, 0))
    o_cells = 64
    o_slots = o_cells + len(cells)
    o_cram = o_slots + 128
    o_zero = o_cram + 128
    total = o_zero + 8192
    hdr = struct.pack(">4sHHIIIIIII", b"SCMG", 1, ncells, total, o_cells, o_slots, o_zero, o_zero, o_zero, o_cram)
    hdr += b"menu/SystemText".ljust(28, b"\0")
    assert len(hdr) == 64 and ncells <= 1536
    out = hdr + bytes(cells) + struct.pack(">64H", *slots) + struct.pack(">64H", *cram) + bytes(8192)
    assert len(out) == total
    return out


# REV R4: the credits screen (the Level Select menu's CREDITS line, cd32x/engine/scd_credits.inc): the credits roll up from
# the bottom of the screen on plane B over OpenJazz's plasma (the 32X), as in MicroNut99's Sonic32XPlus1-RealHardware
# (mars/menu.asm, mars/tools/credits_roll.py): every letter shaded by its row from white (top) to blue (bottom), a dark
# shadow one pixel right and down - here in the game's own 8 x 8 font (SystemText.gif), each row doubled (8 x 16).
CREDITS = [["SONIC THE HEDGEHOG CD", "SONIC TEAM  SEGA 1993"],
           ["SONIC CD 2011", "BLIT SOFTWARE  CHRISTIAN WHITEHEAD"],
           ["RETRO ENGINE", 'CHRISTIAN "TAXMAN" WHITEHEAD'],
           ["RSDKV3 DECOMPILATION", "RUBBERDUCKYCOOLY  ST\u00d7TIC"],
           ["SEGA CD AND 32X CODE", "CHILLY WILLY"],
           ["CD32X FRAMEWORK", "VICTOR LUCHITS  VICIIOUS"],
           ["PLASMA", "OPENJAZZ  ALIREZA NEJATI"],
           ["SEGA CD32X PORT", "MICRONUT99"],
           ["THANK YOU FOR PLAYING!"]]
CR_PITCH, CR_GAP, CR_TOP, CR_END = 20, 12, 8, 64  # line to line, extra after a group, before the first, after the last
CR_GLYPHS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"   # the CD player line's letters (plane A, two cells each)
CR_INNER = [(255, 255, 255), (226, 234, 255), (196, 210, 255), (164, 184, 250), (134, 156, 240), (106, 128, 228)]
CR_SHADOW = (14, 16, 48)


def credits_mdg(src):
    """REV R4: G0S254.MDG - the credits screen's Genesis data (md/sgb.h "SCMG", as an act file): the credits picture's
    unique cells, the CD line's letters (two cells each), palette line 1 (the template's colours); the picture's name
    table (40 words a row) in a 'CRDT' block (the header's chunks offset points to it)."""
    w, h, _, px = rp.gif_decode(src.read("Data/Game/SystemText.gif"))

    def glyph(c):  # 8 x 8 bits of character c (the font's normal half); x (multiplication sign): drawn here
        if c == "\u00d7":
            return [[1 if (x == y or x == 6 - y) and 1 <= y <= 5 and 1 <= x <= 5 else 0 for x in range(8)] for y in range(8)]
        k = ord(c) & 0xFF
        x0, y0 = (k & 15) * 8, (k >> 4) * 8
        return [[1 if px[(y0 + y) * w + x0 + x] not in (0, 1) else 0 for x in range(8)] for y in range(8)]

    def put(img, x, y, c):  # a letter 8 x 16 (rows doubled) at x, y: shaded by its row, as the template
        g = glyph(c)
        for yy in range(16):
            shade = min(5, yy * 6 // 14)
            for xx in range(8):
                if g[yy >> 1][xx] and 0 <= x + xx < 320 and 0 <= y + yy < len(img):
                    img[y + yy][x + xx] = 2 + shade

    lines = sum(len(g) for g in CREDITS)
    H = CR_TOP + lines * CR_PITCH + len(CREDITS) * CR_GAP + CR_END
    H = (H + 7) & ~7
    img = [[0] * 320 for _ in range(H)]  # 0 see-through, 1 shadow, 2-7 the letter by row
    y = CR_TOP
    for g in CREDITS:
        for line in g:
            x = (320 - 8 * len(line)) // 2
            for c in line:
                if c != " ":
                    put(img, x, y, c)
                x += 8
            y += CR_PITCH
        y += CR_GAP

    def shadow(im):
        for yy in range(len(im) - 1, 0, -1):
            for xx in range(len(im[0]) - 1, 0, -1):
                if im[yy][xx] == 0 and im[yy - 1][xx - 1] >= 2:
                    im[yy][xx] = 1
    shadow(img)
    cells, index = [bytes(32)], {bytes(32): 0}

    def cell_of(im, ty, tx):
        t = bytes((im[ty * 8 + r][tx * 8 + c] << 4) | im[ty * 8 + r][tx * 8 + c + 1] for r in range(8) for c in range(0, 8, 2))
        if t not in index:
            index[t] = len(cells)
            cells.append(t)
        return index[t]
    rows = H // 8
    name = []
    for ty in range(rows):
        for tx in range(40):
            k = cell_of(img, ty, tx)
            name.append((0x2000 | k) if k else 0)  # palette line 1
    glyph_base = len(cells)
    for c in CR_GLYPHS:  # 8 x 16 each: top cell, bottom cell (the CD line, plane A rows 25 and 26)
        im = [[0] * 8 for _ in range(16)]
        put(im, 0, 0, c)
        shadow(im)
        for t in range(2):
            cells.append(bytes((im[t * 8 + r][c2] << 4) | im[t * 8 + r][c2 + 1] for r in range(8) for c2 in range(0, 8, 2)))
    ncells = len(cells)
    assert ncells <= 1536, ncells
    cram = [0] * 64
    cram[16 + 1] = md_colour(CR_SHADOW)
    for k, c in enumerate(CR_INNER):
        cram[16 + 2 + k] = md_colour(c)
    o_cells = 64
    o_slots = o_cells + 32 * ncells
    o_cram = o_slots + 128
    o_crdt = o_cram + 128
    crdt = b"CRDT" + struct.pack(">HHI", rows, glyph_base, o_crdt + 12) + struct.pack(">%dH" % len(name), *name)
    o_zero = (o_crdt + len(crdt) + 3) & ~3
    total = o_zero + 8192
    hdr = struct.pack(">4sHHIIIIIII", b"SCMG", 1, ncells, total, o_cells, o_slots, o_zero, o_crdt, o_zero, o_cram)
    hdr += b"menu/credits".ljust(28, b"\0")
    out = hdr + b"".join(cells) + struct.pack(">64H", *([0xFFFF] * 64)) + struct.pack(">64H", *cram) + crdt
    out += bytes(o_zero - len(out)) + bytes(8192)
    assert len(out) == total
    return out, rows, ncells


GENDEV = os.environ.get("GENDEV", "/opt/toolchains/sega")
SH_CXX = os.path.join(GENDEV, "sh-elf", "bin", "sh-elf-g++")
SH_OBJCOPY = os.path.join(GENDEV, "sh-elf", "bin", "sh-elf-objcopy")
SH_NM = os.path.join(GENDEV, "sh-elf", "bin", "sh-elf-nm")
# the engine's compile flags (cd32x/Makefile ENGFLAGS, the planes build) - a blob is compiled as the engine is
BLOB_FLAGS = ["-std=c++17", "-DSONICCD32X", "-DSCD32X_SH2", "-DSCD_PLANES=1", "-DSCD_DEMO_PAD=1", "-DSCD32X_START_LIST=1",
              "-DSCD32X_START_STAGE=0", "-m2", "-mb", "-O2", "-fno-exceptions", "-fno-rtti", "-fno-asynchronous-unwind-tables",
              "-fno-unwind-tables", "-ffunction-sections", "-fdata-sections", "-w"]


def engine_sum(path):
    """REV R4: FNV-1a over the engine image's 32-bit words (big-endian) - plat_engine_sum on the SH2"""
    d = open(path, "rb").read()
    h = 0x811C9DC5
    for i in range(len(d) // 4):
        h = ((h ^ struct.unpack_from(">I", d, 4 * i)[0]) * 0x01000193) & 0xFFFFFFFF
    return h


def native_blobs(data, scenes, addr, engine_dir, tmp, jobs, symdir=None):
    """REV R4: {(list, pos): blob bytes} - each scene's own scripts as native code, linked for cart address addr.
    symdir: each blob's function names (nm) as scene_<list>_<pos>.syms there - for tools/sh2emu -syms"""
    from concurrent.futures import ThreadPoolExecutor
    elf, ebin = os.path.join(engine_dir, "engine.elf"), os.path.join(engine_dir, "engine.bin")
    ops = os.path.join(ROOT, "engine", "ScriptNative.ops")
    for f in (elf, ebin, ops, SH_CXX):
        if not os.path.exists(f):
            sys.exit("--native: no %s (build the engine first: tools/build_native.sh, make ... build_engine/engine.elf)" % f)
    total = engine_sum(ebin)
    ii = os.path.join(tmp, "Script_aot.ii")
    subprocess.run([SH_CXX, "-E", "-P", "-std=c++17", "-DSONICCD32X", "-DSCD32X_SH2", "-DSCD_PLANES=1", "-DSCD_AOT_EXTRACT", "-m2",
                    "-mb", "-I" + os.path.join(ROOT, "cd32x", "kobo", "sh2"), os.path.join(ROOT, "engine", "Script.cpp"), "-o", ii],
                   check=True)

    def one(lp):
        l, p = lp
        base = os.path.join(tmp, "blob_%d_%d" % (l, p))
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "scd_aot.py"), "--data", data, "--ii", ii, "--scene",
                            "%d:%d" % (l, p), "--out", base + ".cpp", "--part", "stage", "--ops", ops, "--engine-sum",
                            "0x%08X" % total], capture_output=True, text=True)
        if r.returncode:
            return lp, None, r.stdout + r.stderr
        r = subprocess.run([SH_CXX] + BLOB_FLAGS + ["-I" + os.path.join(ROOT, "cd32x", "kobo", "sh2"), "-I" + os.path.join(ROOT, "engine"),
                            "-c", base + ".cpp", "-o", base + ".o"], capture_output=True, text=True)
        if r.returncode:
            return lp, None, r.stderr[-2000:]
        r = subprocess.run([SH_CXX, "-m2", "-mb", "-nostartfiles", "-nostdlib", "-T", os.path.join(ROOT, "tools", "native", "native_blob.ld"),
                            "-Wl,--section-start=.blob=0x%08X" % addr, "-Wl,--just-symbols=" + elf, "-Wl,--gc-sections",
                            base + ".o", "-lgcc", "-o", base + ".elf"], capture_output=True, text=True)
        if r.returncode:
            return lp, None, r.stderr[-2000:]
        subprocess.run([SH_OBJCOPY, "-O", "binary", "-j", ".blob", base + ".elf", base + ".bin"], check=True)
        line = open(base + ".cpp").readline().strip()
        blob = open(base + ".bin", "rb").read()
        if symdir:
            r = subprocess.run([SH_NM, "-n", "-C", base + ".elf"], capture_output=True, text=True)
            keep = [ln for ln in r.stdout.splitlines() if len(ln.split()) >= 3 and ln.split()[1] in "Tt" and
                    addr <= int(ln.split()[0], 16) < addr + len(blob)]
            with open(os.path.join(symdir, "scene_%d_%d.syms" % lp), "w") as f:
                f.write("\n".join(keep) + "\n")
        return lp, blob, line
    out = {}
    n = int(jobs[1]) if jobs else (os.cpu_count() or 2)
    with ThreadPoolExecutor(max_workers=n) as pool:
        for lp, blob, note in pool.map(one, sorted(scenes)):
            if blob is None:
                print("  native scripts for %d:%d: not made -\n%s" % (lp[0], lp[1], note))
                continue
            out[lp] = blob
    return out, total


def main():
    args = sys.argv[1:]
    jobs = []
    if "-j" in args:
        i = args.index("-j")
        jobs = ["-j", args[i + 1]]
        del args[i:i + 2]
    scenes_arg = "1:0"
    if "--scenes" in args:
        i = args.index("--scenes")
        scenes_arg = args[i + 1]
        del args[i:i + 2]
    native = None  # REV R4: the engine build folder the scene blobs are linked against
    if "--native" in args:
        i = args.index("--native")
        native = os.path.abspath(args[i + 1])
        del args[i:i + 2]
    pixels = False  # REV R4: the planes build never reads the sprite / tile pixels or the blend tables (parts 14-16)
    if "--with-pixels" in args:  # the drawing build (cd32x/Makefile PLANES=0) needs them
        args.remove("--with-pixels")
        pixels = True
    start = (1, 0)
    if "--start" in args:
        i = args.index("--start")
        start = tuple(int(x) for x in args[i + 1].split(":"))
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    data = os.path.abspath(args[0])
    out = os.path.abspath(args[1] if len(args) > 1 else os.path.join(ROOT, "game_data"))
    os.makedirs(out, exist_ok=True)
    src = sr.Data(data)
    gc = sr.parse_gameconfig(src.read("Data/Game/GameConfig.bin"))
    want = set()  # (list, pos, player)
    drop = set()  # REV R4: "-list:pos" leaves a scene out (as Sonic and as Tails; "-list:pos@1" as Tails only)
    for item in scenes_arg.split(","):
        player = None if item.startswith("-") else 0
        neg = item.startswith("-")
        item = item.lstrip("-")
        if "@" in item:  # REV R3: "1:0@1" = scene 1:0 played as Tails
            item, pl = item.split("@")
            player = int(pl)
            assert player in (0, 1), f"--scenes {item}@{pl}: player 0 (Sonic) or 1 (Tails)"
        l, p = item.split(":")
        l = int(l)
        for pos, _ in enumerate(gc["lists"].get(l, [])):
            if p == "*" or int(p) == pos:
                for pl_ in ((0, 1) if player is None else (player,)):
                    (drop if neg else want).add((l, pos, pl_))
    want -= drop
    assert (start[0], start[1], 0) in want, f"--start {start[0]}:{start[1]} must be one of the scenes (as Sonic)"
    players = sorted({w[2] for w in want})
    subprocess.run(["gcc", "-std=c99", "-O2", os.path.join(ROOT, "tools", "cartimg", "cart_verify_host.c"), os.path.join(ROOT, "md", "cart_verify.c"),
                    "-o", os.path.join(ROOT, "host", "cart_verify_host")], check=True)
    tmp = tempfile.mkdtemp()
    atexit.register(shutil.rmtree, tmp, True)  # REV R4: removed however the run ends (about 2.4 GB with --native)
    # 1. the included scenes' images (the fixed layout must fit ALL of them); REV R3: the Tails scenes' too
    all_parts = {}
    for pl in players:
        imgdir = os.path.join(tmp, f"img{pl}")
        folders = sorted({gc["lists"][l][p][0] for (l, p, q) in want if q == pl})
        extra = (["--player", str(pl)] if pl else []) + ["--only", ",".join(folders)]
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "build_cart_images.py"), data, imgdir] + jobs + extra, check=True)
        for cat in (0, 1, 2, 3):
            for pos, (folder, sid, name) in enumerate(gc["lists"].get(cat, [])):
                f = os.path.join(imgdir, f"{folder}_{sid}.bin")
                if os.path.exists(f) and (cat, pos, pl) in want:
                    parts = read_image(f)
                    # REV R4: no pixels - half the image, half the disc, half the load time. REV R4C: but the scenes the
                    # 32X picture mode draws (PM_LISTS) keep their sheets' and tiles' pixels (not the blend tables)
                    drop = (16,) if pixels or cat in PM_LISTS else (14, 15, 16)
                    if not pixels and cat in PM_LISTS:
                        assert len(parts[15][0]) <= TILESET_SIZE
                    parts = [(b"", es, 0, 0) if k in drop else (d, es, cnt, crc) for k, (d, es, cnt, crc) in enumerate(parts)]
                    all_parts[(cat, pos, pl)] = parts
    # 2. the included scenes' data packs (each checked: SH2 configuration identical on it)
    reports, bads = [], []
    for pl in players:
        packdir = os.path.join(tmp, f"packs{pl}")
        folders = sorted({gc["lists"][l][p][0] for (l, p, q) in want if q == pl})
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "build_stage_packs.py"), data, packdir, "900", "--only", ",".join(folders)] + jobs +
                       (["--player", str(pl)] if pl else []), check=True)
        reports.append((pl, os.path.join(packdir, "stage_packs.md")))
        m = re.search(r"mini-pack not identical on (\d+)", open(reports[-1][1]).read())
        bads.append(int(m.group(1)) if m else -1)
    # 3. REV R4: each scene's own pack as the image's part 17: its mini pack + the dev menu's files, whole
    menu = {f: src.read(f) for f in MENU_FILES}
    packs = {}
    for w in sorted(all_parts):
        l, p, pl = w
        folder, sid, name = gc["lists"][l][p]
        pk = os.path.join(tmp, f"packs{pl}", f"{folder}_{sid}.rsdk")
        files = {}
        if os.path.exists(pk):
            pack = rp.Pack(pk)
            for fname, off, size in pack.files:
                files[fname] = pack.read(fname)
        for f, d in menu.items():
            if d is not None:
                key = next((k for k in files if k.lower() == f.lower()), f)
                files[key] = d
        packs[w] = rp.pack_bytes(files)
        pdata = packs[w]
        all_parts[w] = all_parts[w] + [(pdata, 1, len(pdata), zlib.crc32(pdata) & 0xFFFFFFFF)]
    n = len(next(iter(all_parts.values())))
    # S7e: the fixed layout over the INCLUDED scenes
    incl = list(all_parts.values())
    caps = [max(len(p[k][0]) for p in incl) for k in range(n)]
    if caps[15]:
        caps[15] = TILESET_SIZE  # REV R4C: every tile index reads inside the part (zeros past a scene's own tiles)
    # 3b. REV R4: part 18, the scene's own scripts as native code - linked for where part 18 starts in the cart (the
    # parts before it decide that), the same code for a scene played as Sonic or as Tails (the same bytecode)
    blobs, esum = {}, 0
    if native:
        addr = 0x02080000 + part_offsets(n + 1, caps)[n]
        symdir = os.path.join(out, "native")
        os.makedirs(symdir, exist_ok=True)
        blobs, esum = native_blobs(data, {(l, p) for (l, p, pl) in all_parts}, addr, native, tmp, jobs, symdir)
    for w in sorted(all_parts):
        b = blobs.get(w[:2], b"")
        all_parts[w] = all_parts[w] + [(b, 1, len(b), zlib.crc32(b) & 0xFFFFFFFF)]
    caps.append(max(len(p[n][0]) for p in all_parts.values()))
    n += 1
    # 4. fixed-layout images of the included scenes
    def file_no(l, p, pl):
        num = 0x100 + (l << 8) + p + 128 * pl
        st = 0x100 + (start[0] << 8) + start[1]
        if num == st:
            return 0x100  # R1Q: the start scene is the file the boot loader loads
        if num == 0x100:
            return st     # ... and the title takes the start scene's number
        return num
    rows, sizes = [], set()
    for (l, p, pl) in sorted(want):
        folder, sid, name = gc["lists"][l][p]
        num = file_no(l, p, pl)
        fname = f"L{(num >> 8) - 1}S{num & 0xFF:03d}"
        who = " (Tails)" if pl else ""
        if (l, p, pl) not in all_parts:
            rows.append(f"| {fname} | {folder}/{sid}{who} | no image |")
            continue
        img, hdr = fixed_image(all_parts[(l, p, pl)], caps)
        sizes.add(len(img))
        fn = os.path.join(out, f"{fname}.IMG")
        open(fn, "wb").write(img)
        r = subprocess.run([os.path.join(ROOT, "host", "cart_verify_host"), fn], capture_output=True, text=True)
        rows.append(f"| {fname} | {folder}/{sid}{who} | {len(img) // 1024} KB, pack {len(packs[(l, p, pl)]) // 1024} KB, {r.stdout.strip()} |")
    # 5. SCDDATA.PAK: the start scene's pack (the boot loader loads one; the engine reads the scene image's) - REV R4B:
    #    with the loading picture after it (IMAGE.RAW, the boot loader's copy of it)
    spl, spic = scd_splash.make(scd_splash.title_frame(data))
    open(os.path.join(out, "IMAGE.RAW"), "wb").write(spl)
    pk = packs[(start[0], start[1], 0)]
    pk += b"\0" * (-len(pk) & 3)
    sp = pk + spl + b"SPL1" + struct.pack(">3I", len(pk), len(spl), 0)
    open(os.path.join(out, "SCDDATA.PAK"), "wb").write(sp)
    # 6. REV R4: the Level Select menu's font, the credits screen's Genesis data
    mf = menu_mdg(src)
    open(os.path.join(out, "G0S255.MDG"), "wb").write(mf)
    nimg = len(rows)  # the scene images (the rows below are the two menu act files)
    rows.append(f"| G0S255.MDG | the Level Select menu's font | {len(mf)} bytes |")
    cf, crows, ccells = credits_mdg(src)
    open(os.path.join(out, "G0S254.MDG"), "wb").write(cf)
    rows.append(f"| G0S254.MDG | the credits: {crows} rows, {ccells} cells | {len(cf)} bytes |")
    size = max(sizes) if sizes else 0
    with open(os.path.join(out, "game_data.md"), "w") as w:
        w.write("# Game data for the engine disc\n\n")
        w.write(f"Scene image (fixed layout, every scene the same): {size} bytes of {SCENE_MAX} - {'OK' if size <= SCENE_MAX else 'TOO BIG'}\n\n")
        w.write(f"Scenes: {nimg}. Each image carries its scene's data pack (part 17, at most {caps[17]} bytes) and its chunks' CRCs.\n\n")
        w.write(f"Native scripts (part 18): {len(blobs)} scenes, at most {caps[18]} bytes" + (f", for the engine with checksum {esum:08X}" if blobs else " - none (no --native)") + "\n\n")
        w.write(f"SCDDATA.PAK (the start scene's pack, for the boot loader, and the loading picture): {len(sp)} bytes of {DATA_MAX} - {'OK' if len(sp) <= DATA_MAX else 'TOO BIG'}\n\n")
        w.write(f"IMAGE.RAW (the loading picture: the title screen, frame {scd_splash.FRAME}, {len({c for r in spic for c in r})} colours, and a bar of {scd_splash.SEGS} segments): {len(spl)} bytes\n\n")
        w.write(f"Mini-packs of the included scenes: {'all identical' if bads and all(b == 0 for b in bads) else 'NOT ALL IDENTICAL - see stage_packs*.md'}\n\n")
        w.write("Part capacities (largest over the included scenes): " + ", ".join(str(c) for c in caps) + "\n\n")
        w.write("| File | Scene | Check |\n|---|---|---|\n" + "\n".join(rows) + "\n")
    for pl, rp_ in reports:  # S7d2: /tmp and S: are different file systems
        shutil.copyfile(rp_, os.path.join(out, "stage_packs.md" if not pl else f"stage_packs_p{pl}.md"))
    print(open(os.path.join(out, "game_data.md")).read())
    if size > SCENE_MAX or len(sp) > DATA_MAX:
        sys.exit("*** too big - see above")


if __name__ == "__main__":
    main()
