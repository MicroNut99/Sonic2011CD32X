#!/usr/bin/env python3
"""PORT32X S7e: what the game's scenes DRAW (the start screen first).
  tools/measure_drawing.py Data.rsdk [out .md, default drawing_report.md] [--scenes 0:0,0:1] [frames 600]
Runs the normal PC engine on each scene with SCD32X_DRAWLOG and reports per scene: the draw functions
(calls, per frame max, pixels), the sprite sheets used (size, bytes), the sheet bytes loaded, the 3D
maxima. Decides what the SH2 needs to draw it: graphics into the cart, drawing code, memory."""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scd_report as sr
import measure_stages as ms


def main():
    args = sys.argv[1:]
    scenes = "0:0,0:1"
    if "--scenes" in args:
        i = args.index("--scenes")
        scenes = args[i + 1]
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    data = os.path.abspath(args[0])
    out = args[1] if len(args) > 1 else "drawing_report.md"
    frames = int(args[2]) if len(args) > 2 else 600
    import subprocess, tempfile
    subprocess.run(["make", "-s", "-C", os.path.join(ms.ROOT, "host")], check=True)
    gc = sr.parse_gameconfig(sr.Data(data).read("Data/Game/GameConfig.bin"))
    tmp = tempfile.mkdtemp()
    lines = ["# What the scenes draw (normal PC engine)\n"]
    for item in scenes.split(","):
        l, p = (int(x) for x in item.split(":"))
        folder, sid, name = gc["lists"][l][p]
        d = os.path.join(tmp, f"{l}_{p}")
        log = os.path.join(d, "draw.txt")
        ms.run("soniccd_host", d, data, l, p, frames, {"SCD32X_DRAWLOG": log})
        lines.append(f"\n## L{l}S{p:03d} {folder}/{sid} ({name})\n")
        if not os.path.exists(log):
            lines.append("no drawing log (did the scene run?)\n")
            continue
        fr, fns, sheets, extra = 1, [], [], []
        for t in open(log):
            w = t.split()
            if w[0] == "frames":
                fr = max(1, int(w[1]))
            elif w[0] == "fn":
                fns.append((w[1], int(w[3]), int(w[5]), int(w[7])))
            elif w[0] == "sheet":
                sheets.append(t.strip())
            else:
                extra.append(t.strip())
        lines.append(f"{fr} frames.\n\n| Draw function | calls | per frame (avg / max) | pixels per frame |\n|---|---|---|---|\n")
        for f, c, px, mx in sorted(fns, key=lambda x: -x[1]):
            lines.append(f"| {f} | {c} | {c / fr:.1f} / {mx} | {px // fr} |\n")
        lines.append("\nSprite sheets used:\n\n" + "\n".join(f"- {s}" for s in sheets) + "\n\n" + "\n".join(f"- {e}" for e in extra) + "\n")
    open(out, "w").write("".join(lines))
    print("".join(lines))


if __name__ == "__main__":
    main()
