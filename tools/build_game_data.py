#!/usr/bin/env python3
"""PORT32X S7d: the game's data for the engine disc.
  tools/build_game_data.py Data.rsdk [out dir, default game_data] [--scenes 0:*,1:0] [-j cores]
--scenes: which scenes go on the disc - "list:pos" or "list:*" (default 0:0: the game's first scene).
Output (cd32x/Makefile ENGINE=1 puts it on the disc):
  L<list>S<pos>.IMG   every scene's console cart image in the FIXED layout: each part at the same
                      offset in every image (sized to the largest INCLUDED scene), because the
                      engine binds those addresses once at start-up; at each scene change the
                      engine has the Sub-CPU load the scene's file into cart 0x080000 (max 2.5 MB)
  SCDDATA.PAK         one data pack with the files of all included scenes (the reader reads the
                      pack's directory once) - cart 0x300000 (max 1 MB)
  game_data.md        what is in it, and checks: CRCs of every fixed image, sizes"""
import os, re, shutil, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk_pack as rp
import scd_report as sr
import measure_stages as ms

ROOT = ms.ROOT
SCENE_MAX = 0x300000 - 0x080000  # S7e: the scene image now carries sprite and tile pixels
DATA_MAX = 0x400000 - 0x300000


def read_image(path):
    d = open(path, "rb").read()
    assert d[:4] == b"SCCB" and struct.unpack(">I", d[4:8])[0] == 2, path
    n = struct.unpack(">I", d[8:12])[0]
    parts = []
    for k in range(n):
        off, size, es, cnt, crc = struct.unpack(">5I", d[12 + 20 * k:32 + 20 * k])
        parts.append((d[off:off + size], es, cnt, crc))
    return parts


def fixed_image(parts, caps):
    n = len(parts)
    out = bytearray(12 + 20 * n)
    out[0:4] = b"SCCB"
    out[4:8] = struct.pack(">I", 2)
    out[8:12] = struct.pack(">I", n)
    pos = (len(out) + 15) & ~15
    for k, (data, es, cnt, crc) in enumerate(parts):
        assert len(data) <= caps[k]
        out[12 + 20 * k:32 + 20 * k] = struct.pack(">5I", pos, len(data), es, cnt, crc)
        pos += (caps[k] + 15) & ~15
    out.extend(b"\0" * (pos - len(out)))
    hdr = 12 + 20 * n
    for k, (data, es, cnt, crc) in enumerate(parts):
        off = struct.unpack(">I", out[12 + 20 * k:16 + 20 * k])[0]
        out[off:off + len(data)] = data
    return bytes(out), hdr


def main():
    args = sys.argv[1:]
    jobs = []
    if "-j" in args:
        i = args.index("-j")
        jobs = ["-j", args[i + 1]]
        del args[i:i + 2]
    scenes_arg = "0:0"  # S7e: the game's first scene (the title)
    if "--scenes" in args:
        i = args.index("--scenes")
        scenes_arg = args[i + 1]
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    data = os.path.abspath(args[0])
    out = os.path.abspath(args[1] if len(args) > 1 else os.path.join(ROOT, "game_data"))
    os.makedirs(out, exist_ok=True)
    gc = sr.parse_gameconfig(sr.Data(data).read("Data/Game/GameConfig.bin"))
    want = set()
    for item in scenes_arg.split(","):
        l, p = item.split(":")
        l = int(l)
        for pos, _ in enumerate(gc["lists"].get(l, [])):
            if p == "*" or int(p) == pos:
                want.add((l, pos))
    subprocess.run(["gcc", "-std=c99", "-O2", os.path.join(ROOT, "tools", "cartimg", "cart_verify_host.c"), os.path.join(ROOT, "md", "cart_verify.c"),
                    "-o", os.path.join(ROOT, "host", "cart_verify_host")], check=True)
    tmp = tempfile.mkdtemp()
    imgdir, packdir = os.path.join(tmp, "img"), os.path.join(tmp, "packs")
    # 1. every stage's image (the fixed layout must fit ALL of them)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "build_cart_images.py"), data, imgdir] + jobs, check=True)
    all_parts = {}
    for cat in (0, 1, 2, 3):
        for pos, (folder, sid, name) in enumerate(gc["lists"].get(cat, [])):
            f = os.path.join(imgdir, f"{folder}_{sid}.bin")
            if os.path.exists(f):
                all_parts[(cat, pos)] = read_image(f)
    n = len(next(iter(all_parts.values())))
    # S7e: the fixed layout over the INCLUDED scenes (pixel parts make all 87 together far too big)
    incl = [all_parts[w] for w in want if w in all_parts]
    caps = [max(len(p[k][0]) for p in incl) for k in range(n)]
    # 2. the included scenes' data packs (each checked: SH2 configuration identical on it)
    folders = sorted({gc["lists"][l][p][0] for (l, p) in want})
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "build_stage_packs.py"), data, packdir, "900", "--only", ",".join(folders)] + jobs,
                   check=True)
    report = open(os.path.join(packdir, "stage_packs.md")).read()
    bad = re.search(r"mini-pack not identical on (\d+)", report)
    # 3. fixed-layout images of the included scenes
    rows, sizes = [], set()
    for (l, p) in sorted(want):
        folder, sid, name = gc["lists"][l][p]
        if (l, p) not in all_parts:
            rows.append(f"| L{l}S{p:03d} | {folder}/{sid} | no image |")
            continue
        img, hdr = fixed_image(all_parts[(l, p)], caps)
        sizes.add(len(img))
        fn = os.path.join(out, f"L{l}S{p:03d}.IMG")
        open(fn, "wb").write(img)
        r = subprocess.run([os.path.join(ROOT, "host", "cart_verify_host"), fn], capture_output=True, text=True)
        rows.append(f"| L{l}S{p:03d} | {folder}/{sid} | {len(img) // 1024} KB, {r.stdout.strip()} |")
    # 4. one data pack: every file of the included scenes, at its longest prefix
    files = {}
    for (l, p) in sorted(want):
        folder, sid, name = gc["lists"][l][p]
        pk = os.path.join(packdir, f"{folder}_{sid}.rsdk")
        if os.path.exists(pk):
            pack = rp.Pack(pk)
            for fname, off, size in pack.files:
                d = pack.read(fname)
                if len(d) > len(files.get(fname, b"")):
                    files[fname] = d
    rp.write_pack(os.path.join(out, "SCDDATA.PAK"), files)
    dsize = os.path.getsize(os.path.join(out, "SCDDATA.PAK"))
    size = max(sizes) if sizes else 0
    with open(os.path.join(out, "game_data.md"), "w") as w:
        w.write("# Game data for the engine disc\n\n")
        w.write(f"Scene image (fixed layout, every scene the same): {size} bytes of {SCENE_MAX} - {'OK' if size <= SCENE_MAX else 'TOO BIG'}\n\n")
        w.write(f"SCDDATA.PAK: {len(files)} files, {dsize} bytes of {DATA_MAX} - {'OK' if dsize <= DATA_MAX else 'TOO BIG'}\n\n")
        w.write(f"Mini-packs of the included scenes: {'all identical' if bad and bad.group(1) == '0' else 'NOT ALL IDENTICAL - see stage_packs.md'}\n\n")
        w.write("Part capacities (largest over all stages): " + ", ".join(str(c) for c in caps) + "\n\n")
        w.write("| File | Scene | Check |\n|---|---|---|\n" + "\n".join(rows) + "\n")
    shutil.copyfile(os.path.join(packdir, "stage_packs.md"), os.path.join(out, "stage_packs.md"))  # S7d2: /tmp and S: are different file systems
    print(open(os.path.join(out, "game_data.md")).read())


if __name__ == "__main__":
    main()
