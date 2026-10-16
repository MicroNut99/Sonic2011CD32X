#!/usr/bin/env python3
"""PORT32X: static memory report of a linked engine build.
Lists every global object of at least MIN bytes (BSS + data), biggest first, and
the totals. Usage: tools/mem_report.py host/soniccd_host [min_bytes]
The host build is 64-bit: pointers count 8 bytes here, 4 on the SH2, so arrays of
pointers come out a little bigger than they will be on the 32X."""
import subprocess, sys
binary = sys.argv[1]
minsize = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
out = subprocess.run(["nm", "-S", "-C", "--size-sort", binary], capture_output=True, text=True).stdout
rows, total, small = [], 0, 0
for line in out.splitlines():
    parts = line.split(None, 3)
    if len(parts) < 4 or parts[2] not in "bBdD":
        continue
    size = int(parts[1], 16)
    total += size
    if size >= minsize:
        rows.append((size, parts[3]))
    else:
        small += size
rows.sort(reverse=True)
print(f"{'KB':>8}  object")
for size, name in rows:
    print(f"{size/1024:8.1f}  {name}")
print(f"{small/1024:8.1f}  (all objects under {minsize} bytes)")
print(f"{total/1024:8.1f}  TOTAL static (BSS + data) = {total/1048576:.2f} MB")
print(f"\nSH2 SDRAM is 256 KB for code, data and stack together.")
