#!/usr/bin/env python3
"""PORT32X S7e6: which engine function a STATE code from the PC sampler (8xxx / 7xxx) points at.
  python3 tools/where.py 8A3F 8A40 ...   (codes as Kobo's line 10 shows them)
Reads cd32x/build_engine/engine_symbols.txt of the LAST build - run it before building again."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
syms = []
for line in open(os.path.join(here, "..", "cd32x", "build_engine", "engine_symbols.txt"), errors="replace"):
    p = line.split(None, 2)
    if len(p) == 3 and p[1] in "tTwW":
        try:
            syms.append((int(p[0], 16), p[2].strip()))
        except ValueError:
            pass
syms.sort()
for code in sys.argv[1:]:
    c = int(code, 16)
    if c & 0x8000:
        a = 0x02000000 + (c & 0x7FFF) * 16
    elif (c & 0xF000) == 0x7000:
        a = 0x06000000 + (c & 0xFFF) * 64
        print(f"{code}: SDRAM {a:08X} (the boot loader / Kobo code)")
        continue
    else:
        print(f"{code}: not a PC sample")
        continue
    best = None
    for addr, name in syms:
        if addr <= a:
            best = (addr, name)
        else:
            break
    print(f"{code}: {a:08X} -> {best[1] if best else '?'} (+{a - best[0]:#x})" if best else f"{code}: {a:08X} -> ?")
