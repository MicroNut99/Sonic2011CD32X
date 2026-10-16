#!/usr/bin/env python3
"""REV R0: one table row from a tools/run_md_check.sh log (see tools/survey_md.sh)."""
import re, sys
i, path = sys.argv[1], sys.argv[2]
t = open(path).read()
m = re.search(r'stage (\S+) converted: (\d+) palette lines, (\d+) cells', t)
s = re.search(r'summary: (\d+) frames with layers, (\d+) compared, (\d+) with differences \((\d+) pixels\), (\d+) unsupported', t)
st = re.search(r'plane A avg (\d+) max (\d+), plane B avg (\d+) max (\d+); rows the VDP cannot show: (\d+)', t)
an = re.search(r'animated-tile updates (\d+) \((\d+) new cells\), layout changes (\d+)', t)
if not (m and s and st and an):
    print(f"| {i} | (did not load) | | | | | | | | |")
    sys.exit()
print(f"| {i} | {m.group(1)} | {m.group(2)} | {m.group(3)} | {s.group(2)} | {s.group(3)} | {s.group(5)} | "
      f"{st.group(1)} / {st.group(3)} | {st.group(5)} | {an.group(1)} / {an.group(3)} |")
