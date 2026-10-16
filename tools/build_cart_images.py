#!/usr/bin/env python3
"""PORT32X: S6a - build the console cart image of every stage and report the cart budget.
  tools/build_cart_images.py Data.rsdk [out dir, default cart_images] [-j cores]
For each stage the PC host build loads it (a few frames) and writes its PC cart image; host/cart_build
converts that to the SH2 image (big-endian, trimmed) - out/<folder>_<id>.bin. The report
(out/cart_budget.md) gives each image's size, each act's total over its time periods (A-D: time
travel must not need the CD), and the parts that are byte-identical in every stage (store once)."""
import hashlib, os, re, struct, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scd_report as sr
import measure_stages as ms

ROOT = ms.ROOT
CART = 4 * 1024 * 1024
NAMES = ["scriptcode", "jumptable", "chunks", "collision", "scriptframes", "animframes", "animlist", "arctan", "stagelist",
         "typenames", "globalnames", "animfiles", "font", "trig", "gfxdata", "tilegfx", "blend"]


def parts_of(path):
    d = open(path, "rb").read()
    assert d[:4] == b"SCCB", path
    n = struct.unpack(">I", d[8:12])[0]
    out = []
    for k in range(n):
        off, size, es, cnt, crc = struct.unpack(">5I", d[12 + 20 * k:32 + 20 * k])
        out.append(d[off:off + size])
    return len(d), out


def one(job):
    cat, pos, folder, sid, data, tmp, outdir = job
    d = os.path.join(tmp, f"{cat}_{pos}")
    img = os.path.join(d, "cart.img")
    ms.run("soniccd_host", d, data, cat, pos, 3, {"SCD32X_CARTIMG_OUT": img})
    if not os.path.exists(img + ".0"):
        return (folder, sid, None, "no stage load")
    out = os.path.join(outdir, f"{folder}_{sid}.bin")
    r = subprocess.run([os.path.join(ROOT, "host", "cart_build"), img + ".0", out], capture_output=True, text=True)
    if r.returncode:
        return (folder, sid, None, r.stderr.strip())
    return (folder, sid, out, "")


def main():
    args = sys.argv[1:]
    jobsN = os.cpu_count() or 4
    if "-j" in args:
        i = args.index("-j")
        jobsN = int(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    data = os.path.abspath(args[0])
    outdir = os.path.abspath(args[1] if len(args) > 1 else os.path.join(ROOT, "cart_images"))
    os.makedirs(outdir, exist_ok=True)
    subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "host")], check=True)
    subprocess.run(["g++", "-std=c++17", "-O2", "-DSONICCD32X", "-w", "-I", ROOT, os.path.join(ROOT, "tools", "cartimg", "cart_build.cpp"),
                    "-o", os.path.join(ROOT, "host", "cart_build")], check=True)
    chk = subprocess.run([os.path.join(ROOT, "host", "cart_build"), "--check"], capture_output=True, text=True)
    print(chk.stdout.strip().splitlines()[-1])
    if chk.returncode:
        print(chk.stdout)
        sys.exit(1)
    gc = sr.parse_gameconfig(sr.Data(data).read("Data/Game/GameConfig.bin"))
    tmp = tempfile.mkdtemp()
    jobs = [(cat, pos, folder, sid, data, tmp, outdir) for cat in (1, 3, 0) for pos, (folder, sid, name) in enumerate(gc["lists"].get(cat, []))]
    with ThreadPoolExecutor(max_workers=jobsN) as pool:
        results = list(pool.map(one, jobs))
    rows, hashes = [], {}
    for folder, sid, out, err in results:
        if not out:
            rows.append((f"{folder}/{sid}", None, None, err))
            continue
        total, parts = parts_of(out)
        rows.append((f"{folder}/{sid}", total, [len(p) for p in parts], ""))
        for k, p in enumerate(parts):
            hashes.setdefault(k, set()).add(hashlib.sha1(p).hexdigest())
    good = [r for r in rows if r[1]]
    # S6a2: the script code all stages share at the start (the global scripts)
    codes = [parts_of(out)[1][0] for (folder, sid, out, err) in results if out and re.match(r"R\d\d[ABCD]$", folder)]
    common = 0
    if codes:
        first = codes[0]
        common = min(len(c) for c in codes)
        for c in codes[1:]:
            i = 0
            while i < common and c[i] == first[i]:
                i += 1
            common = i
    # S6a2: parts identical across the time periods of each act
    perAct = {}
    for folder, sid, out, err in results:
        m = re.match(r"(R\d\d)([ABCD])$", folder)
        if out and m:
            perAct.setdefault(m.group(1), []).append([hashlib.sha1(p).hexdigest() for p in parts_of(out)[1]])
    sameInAct = {}
    for a, hs in perAct.items():
        if len(hs) > 1:
            sameInAct[a] = [NAMES[k] for k in range(len(NAMES)) if len({h[k] for h in hs}) == 1]
    shared = [k for k in range(len(NAMES)) if len(hashes.get(k, {0, 1})) == 1]
    sharedBytes = sum(good[0][2][k] for k in shared) if good else 0
    acts = {}
    for label, total, sizes, _ in good:
        m = re.match(r"(R\d\d)([ABCD])/", label)
        if m:
            acts.setdefault(m.group(1), []).append(total - sharedBytes)
    with open(os.path.join(outdir, "cart_budget.md"), "w") as w:
        w.write("# Console cart images (big-endian, trimmed)\n\n")
        w.write(f"{len(good)} of {len(rows)} stages built. Parts identical in every stage (store once): "
                f"{', '.join(NAMES[k] for k in shared) or 'none'} = {sharedBytes // 1024} KB.\n\n")
        w.write(f"Script code shared by every regular stage at the start (the global scripts): {common // 1024} KB "
                f"- storing it once would save that per stage image.\n\n")
        if sameInAct:
            w.write("Parts identical in all time periods of an act (store once per act):\n\n")
            for a in sorted(sameInAct):
                w.write(f"- {a}: {', '.join(sameInAct[a]) or 'none'}\n")
            w.write("\n")
        if acts:
            worst = max(acts.items(), key=lambda a: sum(a[1]))
            w.write(f"Largest act over its time periods (without the shared parts): {worst[0]} = {sum(worst[1]) // 1024} KB "
                    f"({len(worst[1])} periods) of the {CART // 1024} KB cart (engine code and shared parts come on top).\n\n")
            w.write("| Act | Periods | Total KB |\n|---|---|---|\n")
            for a, v in sorted(acts.items()):
                w.write(f"| {a} | {len(v)} | {sum(v) // 1024} |\n")
            w.write("\n")
        w.write("| Stage | Image KB | " + " | ".join(NAMES) + " |\n|" + "---|" * (len(NAMES) + 2) + "\n")
        for label, total, sizes, err in rows:
            if not total:
                w.write(f"| {label} | {err} |" + " |" * len(NAMES) + "\n")
            else:
                w.write(f"| {label} | {total // 1024} | " + " | ".join(str(s // 1024) for s in sizes) + " |\n")
    print(f"{len(good)} images in {outdir}; report {os.path.join(outdir, 'cart_budget.md')}")


if __name__ == "__main__":
    main()
