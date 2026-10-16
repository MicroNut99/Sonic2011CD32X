#!/usr/bin/env python3
"""PORT32X S7e: the game's own pictures, drawn the way the console will draw them - proven on the PC.
  tools/test_draw.py Data.rsdk [out dir, default draw_test] [--scene 0:0] [--frames 300]
1. reference: the normal engine with 198 screen lines (host/soniccd_host_ys198): it writes the cart
   images (now with the sprite / tile pixels and the blend tables) and a CRC of every 10th frame
2. the SH2 drawing configuration (host/soniccd_host_sh2draw_asan): renderer on, every pixel and table
   read from those cart images, small arrays - the same CRCs must come out, no ASan error
3. out/draw_test.md (identical or the first frame that differs) and out/frames/*.png: every 10th frame
   of the drawing configuration = what the console will show"""
import os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure_stages as ms
import scd_report as sr

ROOT = ms.ROOT


def main():
    args = sys.argv[1:]
    scene, frames = "0:0", 300
    if "--scene" in args:
        i = args.index("--scene")
        scene = args[i + 1]
        del args[i:i + 2]
    align = "--align" in args  # S7e5: run the drawing configuration with the alignment checker instead of ASan
    if align:
        args.remove("--align")
    if "--frames" in args:
        i = args.index("--frames")
        frames = int(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    data = os.path.abspath(args[0])
    out = os.path.abspath(args[1] if len(args) > 1 else os.path.join(ROOT, "draw_test"))
    cat, pos = (int(x) for x in scene.split(":"))
    for v in ([], ["SCREEN198=1"], ["DRAW=1", "ALIGN=1"] if align else ["DRAW=1", "ASAN=1"]):
        subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "host")] + v, check=True)
    gc = sr.parse_gameconfig(sr.Data(data).read("Data/Game/GameConfig.bin"))
    folder, sid, name = gc["lists"][cat][pos]
    tmp = tempfile.mkdtemp()
    img = os.path.join(tmp, "cart.img")
    os.makedirs(os.path.join(tmp, "rf"))
    os.makedirs(os.path.join(tmp, "df"))
    env = {"SCD32X_SAVE_EVERY": "10"}
    ms.run("soniccd_host_ys198", os.path.join(tmp, "r"), data, cat, pos, frames,
           dict(env, SCD32X_CARTIMG_OUT=img, SCD32X_CRC_LOG=os.path.join(tmp, "r.crc"), SCD32X_FRAMES_DIR=os.path.join(tmp, "rf")))
    ms.run("soniccd_host_sh2draw_align" if align else "soniccd_host_sh2draw_asan", os.path.join(tmp, "d"), data, cat, pos, frames,
           dict(env, SCD32X_CARTIMG_IN=img, SCD32X_CRC_LOG=os.path.join(tmp, "d.crc"), SCD32X_FRAMES_DIR=os.path.join(tmp, "df"),
                SCD32X_DOUBLEBUF="1"))  # S7e3: two buffers, the second junk - as on the 32X
    r = open(os.path.join(tmp, "r.crc")).read().split("\n") if os.path.exists(os.path.join(tmp, "r.crc")) else []
    d = open(os.path.join(tmp, "d.crc")).read().split("\n") if os.path.exists(os.path.join(tmp, "d.crc")) else []
    outtxt = open(os.path.join(tmp, "d", "out.txt"), errors="replace").read() if os.path.exists(os.path.join(tmp, "d", "out.txt")) else ""
    asan = "ERROR: AddressSanitizer" in outtxt
    misaligned = sorted(set(l.split(": runtime error")[0].split("/")[-1] + ":" + l.split("runtime error: ")[1][:90]
                            for l in outtxt.splitlines() if "runtime error" in l and "misaligned" in l))
    first = next((i for i, (a, b) in enumerate(zip(r, d)) if a != b), None)
    n = min(len(r), len(d))
    if asan:
        verdict = "ASAN ERROR in the drawing configuration"
    elif not r or not d or n < 2:
        verdict = "NO FRAMES (a run stopped early)"
    elif first is None and len(r) == len(d):
        verdict = f"identical: {n - 1} compared frames (every 10th of {frames})"
    else:
        verdict = f"DIFFERS from compared frame {first} (line '{r[first] if first is not None and first < len(r) else ''}' vs '{d[first] if first is not None and first < len(d) else ''}')"
    os.makedirs(os.path.join(out, "frames"), exist_ok=True)
    for f in os.listdir(os.path.join(tmp, "df")):
        shutil.copyfile(os.path.join(tmp, "df", f), os.path.join(out, "frames", f))
    with open(os.path.join(out, "draw_test.md"), "w") as w:
        w.write(f"# The game's own pictures: {folder}/{sid} (list {cat}, scene {pos}), {frames} frames, 198 lines\n\n")
        w.write(f"Drawing configuration (console, two alternating frame buffers, the second filled with junk) against the normal engine: **{verdict}**\n\n")
        if asan:
            w.write("```\n" + outtxt[outtxt.index("ERROR: AddressSanitizer"):][:3000] + "\n```\n")
        if align:
            w.write(f"Misaligned 16/32-bit accesses (the SH2 faults on these): {len(misaligned)}\n\n")
            for m in misaligned[:40]:
                w.write(f"- {m}\n")
            w.write("\n")
        w.write(f"Pictures of the drawing configuration: {out}/frames (every 10th frame)\n")
    print(open(os.path.join(out, "draw_test.md")).read())


if __name__ == "__main__":
    main()
