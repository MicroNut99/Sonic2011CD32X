#!/usr/bin/env python3
"""PORT32X: S5e - run EVERY stage of the game in the host build (spin-dash pad) and report:
  1. the real maxima the engine uses (object slots, draw lists, 3D vertices/faces, menu rows,
     sound effects, sprite/animation frames, script size, layout rows) per stage and overall;
  2. whether the SH2 configuration (no-draw, small arrays, cart image, layout pool) gives the
     same camera log as the normal engine on that stage, and any PORT32X messages it printed.
  tools/measure_stages.py Data.rsdk [frames per stage, default 900] [report.md] [-j cores]
Stages run in parallel (all cores by default) and in light mode (camera log + maxima, no plane
check) - S5m.
Needs host/soniccd_host and host/soniccd_host_sh2cfg_asan (the script builds them)."""
import os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scd_report as sr

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CATS = {0: "presentation", 1: "regular", 2: "bonus", 3: "special"}
FIELDS = ["objslot", "objects", "drawlists", "vertices", "faces", "menurows", "sfx", "scriptframes", "animframes", "animations",
          "code", "jumps", "fgrows", "bgrows", "menutext", "savetop", "poolkb", "saveidx", "saveblocks"]


def pad(frames):
    p, t = ["t3500:-"], 4000
    while t < frames * 1000 // 60:
        p.append(f"t{t}:D,t{t + 150}:D+C,t{t + 300}:D,t{t + 900}:R,t{t + 2400}:R+C,t{t + 2700}:R,t{t + 3900}:-")
        t += 4000
    return ",".join(p)


def run(binary, folder, data, cat, pos, frames, extra):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "settings.ini"), "w") as f:
        f.write(f"[Dev]\nStartingCategory={cat}\nStartingScene={pos}\nDataFile={data}\n")
    env = dict(os.environ, SCD32X_MDCHECK="1", SCD32X_MDCHECK_LIGHT="1", SCD32X_MAX_FRAMES=str(frames), SCD32X_PAD=pad(frames),
               SCD32X_CAMLOG="camlog.txt", SCD32X_EVENT_LOG="events.txt", **extra)
    with open(os.path.join(folder, "out.txt"), "w") as out:
        subprocess.run([os.path.join(ROOT, "host", binary)], cwd=folder, env=env, stdout=out, stderr=subprocess.STDOUT, timeout=600)
    ev = open(os.path.join(folder, "events.txt"), errors="replace").read() if os.path.exists(os.path.join(folder, "events.txt")) else ""
    return ev


def one_stage(job):
    cat, pos, folder, sid, data, frames, tmp = job
    label = f"{folder}/{sid}"
    d = os.path.join(tmp, f"{cat}_{pos}")
    img = os.path.join(d, "cart.img")
    ev = run("soniccd_host", os.path.join(d, "n"), data, cat, pos, frames, {"SCD32X_CARTIMG_OUT": img})
    m = re.search(r"MAXIMA \S+ (.*)", ev)
    vals = {}
    if m:
        toks = m.group(1).split()
        for k, v in zip(toks[0::2], toks[1::2]):
            vals[k] = v
    ev2 = run("soniccd_host_sh2cfg_asan", os.path.join(d, "s"), data, cat, pos, frames, {"SCD32X_CARTIMG_IN": img})
    out2 = open(os.path.join(d, "s", "out.txt"), errors="replace").read()
    asan = "ERROR: AddressSanitizer" in out2
    c1 = os.path.join(d, "n", "camlog.txt")
    c2 = os.path.join(d, "s", "camlog.txt")
    same = os.path.exists(c1) and os.path.exists(c2) and open(c1).read() == open(c2).read()
    if not os.path.exists(img + ".0"):
        verdict = "no stage load"
    elif asan:
        verdict = "ASAN ERROR"
    else:
        verdict = "identical" if same else "DIFFERS"
    msgs = sorted(set(re.findall(r"PORT32X: [^\n]*", ev2 + out2)))
    detail = ""
    if verdict == "DIFFERS":
        l1 = open(c1).read().splitlines()
        l2 = open(c2).read().splitlines()
        for i in range(min(len(l1), len(l2))):
            if l1[i] != l2[i]:
                t1, t2 = l1[i].split(), l2[i].split()
                names = []
                for a_, b_ in zip(t1[1:], t2[1:]):
                    if a_ != b_:
                        key = a_[:1]  # x, y, L, s<slot>:, P, E, F, H - the checksum after it may start with a letter
                        if key == "s":
                            names.append("layer slot " + a_[1:a_.index(":")] + " scroll")
                        else:
                            names.append({"x": "camera x", "y": "camera y", "L": "active layers", "P": "palettes",
                                          "E": "object state", "F": "foreground layout", "H": "hitboxes/sheets"}.get(key, key))
                if len(t1) != len(t2):
                    names.append("layers drawn")
                detail = f"frame {t1[0]}: " + ", ".join(dict.fromkeys(names))
                break
        else:
            detail = f"one log is shorter ({len(l1)} vs {len(l2)} frames)"
    elif verdict == "ASAN ERROR":
        m1 = re.search(r"ERROR: AddressSanitizer: ([\w-]+)", out2)
        m2 = re.search(r"#\d+ 0x[0-9a-f]+ in (\S+)[^\n]*?((?:engine|host)/[\w./]+:\d+)", out2)
        detail = (m1.group(1) if m1 else "?") + (f" in {m2.group(1)} {m2.group(2)}" if m2 else "")
    return (CATS[cat], label, vals, verdict + (f" ({detail})" if detail else ""), msgs)


def main():
    args = [a for a in sys.argv[1:]]
    jobsN = os.cpu_count() or 4
    if "-j" in args:
        i = args.index("-j")
        jobsN = int(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    data = os.path.abspath(args[0])
    frames = int(args[1]) if len(args) > 1 else 900
    report = args[2] if len(args) > 2 else os.path.join(ROOT, "stages_report.md")
    subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "host")], check=True)
    subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "host"), "SH2CFG=1", "ASAN=1"], check=True)
    gc = sr.parse_gameconfig(sr.Data(data).read("Data/Game/GameConfig.bin"))
    tmp = tempfile.mkdtemp()
    jobs = [(cat, pos, folder, sid, data, frames, tmp) for cat in (1, 3, 0)
            for pos, (folder, sid, name) in enumerate(gc["lists"].get(cat, []))]
    print(f"{len(jobs)} stages, {jobsN} at a time", flush=True)
    rows = []
    with ThreadPoolExecutor(max_workers=jobsN) as pool:
        for r in pool.map(one_stage, jobs):  # results arrive in stage order
            rows.append(r)
            print(f"{r[0]:12s} {r[1]:12s} {r[3]}", flush=True)
    overall, mismatches = {}, 0
    for cat, label, vals, verdict, msgs in rows:
        if verdict.startswith(("ASAN ERROR", "DIFFERS")):
            mismatches += 1
        for k, v in vals.items():
            if k == "drawlists":
                overall[k] = max(overall.get(k, 0), max(int(x) for x in v.split(",")))
            elif k == "saveblocks":
                overall[k] = format(int(overall.get(k, "0"), 16) | int(v, 16), "08x")
            else:
                overall[k] = max(overall.get(k, 0), int(v))
    with open(report, "w") as w:
        w.write(f"# Every stage: maxima and the SH2 configuration ({frames} frames each, spin-dash pad)\n\n")
        w.write(f"{len(rows)} stages; SH2 configuration differs / ASan errors on {mismatches}.\n\n")
        w.write("Overall maxima: " + ", ".join(f"{k} {overall.get(k, '?')}" for k in FIELDS) + "\n\n")
        w.write("| List | Stage | SH2 config | " + " | ".join(FIELDS) + " | PORT32X messages |\n|" + "---|" * (len(FIELDS) + 4) + "\n")
        for cat, label, vals, verdict, msgs in rows:
            w.write(f"| {cat} | {label} | {verdict} | " + " | ".join(vals.get(k, "") for k in FIELDS) + " | " + "; ".join(m[9:] for m in msgs) + " |\n")
    print(f"\nreport: {report}  ({mismatches} stages differ or have ASan errors)")


if __name__ == "__main__":
    main()
