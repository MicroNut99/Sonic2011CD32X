#!/usr/bin/env python3
"""PORT32X: S7a - every stage's data-pack subset for the cart (the SH2 has no file system).
  tools/build_stage_packs.py Data.rsdk [out dir, default stage_packs] [frames, default 900] [-j cores]
                             [--only R11A,R71A,...]   (just these stages)
                             [--player N]             (REV R3: played as player N, 1 = Tails)
Per stage, three runs of the host builds:
  1. normal build: the stage's cart image + camera log
  2. SH2 configuration on the full Data.rsdk (cart image from 1): logs every file it reads and the
     highest position read in each (SCD32X_FILELOG) - what the SH2 really reads (no pixels, no
     chunk / collision files: those come from the cart image)
  3. SH2 configuration on the MINI-PACK built from that log (each file cut after its last byte read,
     re-encrypted for its new size): its camera log must equal run 1's
Output: out/<folder>_<id>.rsdk and out/stage_packs.md (per stage: files, KB, identical?)."""
import os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk_pack as rp
import scd_report as sr
import measure_stages as ms

ROOT = ms.ROOT


def same(a, b):
    return os.path.exists(a) and os.path.exists(b) and open(a).read() == open(b).read()


def one(job):
    cat, pos, folder, sid, data, frames, tmp, outdir, player = job
    d = os.path.join(tmp, f"{cat}_{pos}")
    img = os.path.join(d, "cart.img")
    log = os.path.join(d, "files.txt")
    pl = {"SCD32X_PLAYER": str(player)} if player else {}  # REV R3: every run as that character
    ms.run("soniccd_host", os.path.join(d, "n"), data, cat, pos, frames, dict(pl, SCD32X_CARTIMG_OUT=img))
    if not os.path.exists(img + ".0"):
        return (f"{folder}/{sid}", None, 0, 0, "no stage load", "")
    ms.run("soniccd_host_sh2cfg_asan", os.path.join(d, "s"), data, cat, pos, frames, dict(pl, SCD32X_CARTIMG_IN=img, SCD32X_FILELOG=log))
    full = same(os.path.join(d, "n", "camlog.txt"), os.path.join(d, "s", "camlog.txt"))
    src = sr.Data(data)
    files = {}
    pastEnd = 0  # S7a4: a position past the file's size = a read logged under the wrong name
    for line in open(log) if os.path.exists(log) else []:
        n, path = line.split(None, 1)
        path = path.strip()
        if src.has(path) and int(n) > src.size(path):
            pastEnd += 1
        if src.has(path):
            files[src.index[path.lower()][0]] = src.read_range(path, 0, min(int(n), src.size(path)))
    pack = os.path.join(outdir, f"{folder}_{sid}.rsdk")
    rp.write_pack(pack, files)
    ms.run("soniccd_host_sh2cfg_asan", os.path.join(d, "m"), pack, cat, pos, frames, dict(pl, SCD32X_CARTIMG_IN=img))
    mini = same(os.path.join(d, "n", "camlog.txt"), os.path.join(d, "m", "camlog.txt"))
    out = open(os.path.join(d, "m", "out.txt"), errors="replace").read()
    asan = "ERROR: AddressSanitizer" in out
    detail = ""
    if asan: # S7a2: what and where
        m1 = re.search(r"ERROR: AddressSanitizer: ([\w-]+)", out)
        frames = re.findall(r"#\d+ 0x[0-9a-f]+ in (\S+)[^\n]*?((?:engine|host)/[\w./]+:\d+)", out)[:3]
        detail = " (" + (m1.group(1) if m1 else "?") + " in " + " <- ".join(f"{f} {l}" for f, l in frames) + ")"
    verdict = ("ASAN ERROR" + detail) if asan else ("identical" if mini else "DIFFERS") + ("" if full else " (full pack differs too)")
    if pastEnd:
        verdict += f" (file log: {pastEnd} reads under a wrong name)"
    big = sorted(files.items(), key=lambda kv: -len(kv[1]))[:3]
    biggest = ", ".join(f"{k.rsplit('/', 1)[-1]} {len(v) // 1024}K" for k, v in big)
    return (f"{folder}/{sid}", pack, len(files), os.path.getsize(pack), verdict, biggest)


def main():
    args = sys.argv[1:]
    jobsN = os.cpu_count() or 4
    only = None
    if "--only" in args:
        i = args.index("--only")
        only = set(args[i + 1].upper().split(","))
        del args[i:i + 2]
    if "-j" in args:
        i = args.index("-j")
        jobsN = int(args[i + 1])
        del args[i:i + 2]
    player = 0
    if "--player" in args:  # REV R3
        i = args.index("--player")
        player = int(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    data = os.path.abspath(args[0])
    outdir = os.path.abspath(args[1] if len(args) > 1 else os.path.join(ROOT, "stage_packs"))
    frames = int(args[2]) if len(args) > 2 else 900
    os.makedirs(outdir, exist_ok=True)
    subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "host")], check=True)
    subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "host"), "SH2CFG=1", "ASAN=1"], check=True)
    gc = sr.parse_gameconfig(sr.Data(data).read("Data/Game/GameConfig.bin"))
    tmp = tempfile.mkdtemp()
    jobs = [(cat, pos, folder, sid, data, frames, tmp, outdir, player) for cat in (1, 3, 0) for pos, (folder, sid, name) in enumerate(gc["lists"].get(cat, []))
            if not only or folder.upper() in only]
    print(f"{len(jobs)} stages, {jobsN} at a time", flush=True)
    rows = []
    with ThreadPoolExecutor(max_workers=jobsN) as pool:
        for r in pool.map(one, jobs):
            rows.append(r)
            print(f"{r[0]:12s} {r[2]:4d} files {r[3] // 1024:6d} KB  {r[4]}", flush=True)
    bad = sum(1 for r in rows if not r[4].startswith("identical"))
    good = [r for r in rows if r[1]]
    with open(os.path.join(outdir, "stage_packs.md"), "w") as w:
        w.write(f"# Stage data packs for the cart ({frames} frames each)\n\n")
        w.write(f"{len(rows)} stages; mini-pack not identical on {bad}. Largest mini-pack: "
                f"{max((r[3] for r in good), default=0) // 1024} KB.\n\n| Stage | SH2 on the mini-pack | Files | KB | Biggest files |\n|---|---|---|---|---|\n")
        for label, pack, n, size, verdict, biggest in rows:
            w.write(f"| {label} | {verdict} | {n} | {size // 1024} | {biggest} |\n")
    print(f"\n{len(rows)} stages, {bad} not identical; report {os.path.join(outdir, 'stage_packs.md')}")


if __name__ == "__main__":
    main()
