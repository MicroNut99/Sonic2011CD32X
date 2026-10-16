#!/bin/sh
# PORT32X S6a: does the SH2 compiler lay out the cart parts exactly as the PC does?
# The same numbers (cart_layout.h: element sizes, field offsets / sizes / counts) are compiled for the
# PC (run) and by sh-elf-g++ (read big-endian out of the object file) and compared one by one.
# Must say "SH2 layout identical". Needs the SH2 toolchain (your PC).
SH=${SH-/opt/toolchains/sega/sh-elf/bin/sh-elf-}
cd "$(dirname "$0")/.."
T=$(mktemp -d)
g++ -std=c++17 -DSONICCD32X -w -I. tools/cartimg/layout_probe.cpp -o $T/probe_pc || { echo "PC probe: compile FAILED"; exit 1; }
$T/probe_pc > $T/pc.txt
${SH}g++ -std=c++17 -DSONICCD32X -DSCD32X_SH2 -DSCD_PROBE_NO_MAIN -m2 -mb -O2 -fno-exceptions -fno-rtti -w -I. \
    -c tools/cartimg/layout_probe.cpp -o $T/probe_sh2.o || { echo "SH2 probe: compile FAILED"; exit 1; }
${SH}objcopy -O binary --only-section=.rodata --only-section=.rodata.* $T/probe_sh2.o $T/probe_sh2.bin
python3 - "$T/pc.txt" "$T/probe_sh2.bin" <<'PY'
import struct, sys
pc = [int(l) for l in open(sys.argv[1]) if l.strip()]
raw = open(sys.argv[2], "rb").read()
n = len(pc)
if len(raw) < 4 * n:
    print(f"SH2 probe: {len(raw)} bytes of data, expected {4 * n} - FAILED"); sys.exit(1)
sh2 = list(struct.unpack(">%di" % n, raw[:4 * n]))
diff = [(i, a, b) for i, (a, b) in enumerate(zip(pc, sh2)) if a != b]
if diff:
    print("SH2 layout DIFFERS (index, PC, SH2):", diff[:10]); sys.exit(1)
print(f"SH2 layout identical: {n} numbers (element sizes, field offsets / sizes / counts)")
PY
r=$?
rm -rf $T
exit $r
