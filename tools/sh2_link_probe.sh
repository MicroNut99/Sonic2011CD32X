#!/bin/bash
# PORT32X: S5a/S5b - link the whole engine for the SH2 (SH2 configuration: no-draw + small arrays) (probe layout, not runnable) and report:
#   sizes per section (code, read-only, initialised data, zeroed arrays), per engine file,
#   the biggest arrays, and every symbol the link could not find. Writes sh2_link.txt.
SH=${SH-/opt/toolchains/sega/sh-elf/bin/sh-elf-}
cd "$(dirname "$0")/.."
OUT=sh2_link.txt
D=build_sh2/probe
rm -rf $D && mkdir -p $D
: > $OUT
ARCH=${ARCH--m2 -mb}
FLAGS="-std=c++17 -DSONICCD32X -DSCD32X_SH2 -Icd32x/kobo/sh2 $ARCH -O2 -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections -w"
FAILED=""
for f in engine/*.cpp cd32x/engine/plat_mars.cpp; do
    o=$D/$(basename $f .cpp).o
    ${SH}g++ $FLAGS -c $f -o $o 2>> $D/errors.txt || FAILED="$FAILED $(basename $f)"
done
if [ -n "$FAILED" ]; then
    echo "== FAILED to compile:$FAILED" | tee -a $OUT
    grep -m 20 error $D/errors.txt | tee -a $OUT
    exit 1
fi
# -nostartfiles: Kobo's crt0.s will start it; libraries: libstdc++ (what the engine still uses),
# newlib libc, libgcc. System calls come from cd32x/engine/plat_mars.cpp (S5a fix: the SH libc's
# own syscalls.o defines errno twice)
${SH}g++ $ARCH -nostartfiles -T cd32x/engine/probe.ld -Wl,-Map=$D/engine.map -Wl,--gc-sections \
    $D/*.o -lstdc++ -lc -lgcc -o $D/engine.elf 2> $D/link.txt
if grep -q "undefined reference" $D/link.txt; then
    echo "== symbols the link could not find (each needs a stub or a platform function):" | tee -a $OUT
    grep -o "undefined reference to \`[^']*'" $D/link.txt | sort | uniq -c | sort -rn | head -40 | tee -a $OUT
fi
if [ ! -f $D/engine.elf ]; then
    echo "== link FAILED - first lines:" | tee -a $OUT
    grep -v "warning" $D/link.txt | head -20 | tee -a $OUT
    # which symbol pulled each library file in (the link map's "Archive member included" list)
    ${SH}g++ $ARCH -nostartfiles -T cd32x/engine/probe.ld -Wl,--trace -Wl,-y,_errno -Wl,-y,__errno \
        -Wl,--gc-sections $D/*.o -lstdc++ -lc -lgcc -o $D/trace.elf > $D/trace.txt 2>&1
    echo "== why lib_a-syscalls.o was pulled in (archive members and the symbols that needed them):" | tee -a $OUT
    python3 - "$D/engine.map" <<'PY' | tee -a $OUT
import sys
lines = open(sys.argv[1], errors="replace").read().split("\n")
for i, l in enumerate(lines):
    if "syscalls.o" in l and i + 1 < len(lines):
        print(l.strip())
        print("    needed by:", lines[i + 1].strip())
PY
    echo "== linker trace (syscalls / errno lines):" | tee -a $OUT
    grep -E "syscalls|errno" $D/trace.txt | head -20 | tee -a $OUT
    exit 1
fi
echo "== sections (bytes)" | tee -a $OUT
${SH}size -A $D/engine.elf | grep -E "^\.(text|rodata|data|bss|init_array|eh_frame)" | tee -a $OUT
echo "== code + read-only data per engine file (bytes)" | tee -a $OUT
${SH}size $D/*.o | sed "s#$D/##" | sort -k1 -n -r | head -12 | tee -a $OUT
echo "== the 30 biggest arrays / variables (bytes)" | tee -a $OUT
${SH}nm -C -S --size-sort $D/engine.elf | grep -i " [bBdD] " | tail -30 | \
    python3 -c 'import sys
for l in reversed(sys.stdin.readlines()):
    p = l.split(None, 3)
    print(f"{int(p[1], 16):9d}  {p[3].strip()}")' | tee -a $OUT
echo "== library pieces pulled in (the biggest)" | tee -a $OUT
python3 - "$D/engine.map" <<'PY' | tee -a $OUT
import re, sys
rows = {}
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"\s+\.text\S*\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S*lib(?:stdc\+\+|c|gcc|nosys)\.a)\((\S+)\)", line)
    if m:
        key = m.group(2).rsplit("/", 1)[-1] + ":" + m.group(3)
        rows[key] = rows.get(key, 0) + int(m.group(1), 16)
for k, v in sorted(rows.items(), key=lambda r: -r[1])[:12]:
    print(f"{v:9d}  {k}")
print(f"{sum(rows.values()):9d}  (all library code)")
PY
echo "== why these library pieces are in (from the link map: member <- the file and symbol that needed it)" | tee -a $OUT
python3 - "$D/engine.map" <<'PY' | tee -a $OUT
import re, sys
lines = open(sys.argv[1], errors="replace").read().split("\n")
try:
    start = next(i for i, l in enumerate(lines) if l.startswith("Archive member included"))
except StopIteration:
    print("(no archive member list in the map)")
    sys.exit(0)
want = ("unwind-dw2", "svfscanf", "svfiscanf", "strtod", "cp-demangle", "vfprintf", "svfprintf",
        "eh_personality", "eh_throw", "eh_catch", "eh_terminate", "atof", "sscanf", "sprintf")
i = start + 1
while i < len(lines) and not lines[i].startswith("Discarded") and not lines[i].startswith("Allocating common"):
    l = lines[i]
    m = re.match(r"^(\S+\.a)\((\S+)\)\s*$", l)
    if m and any(w in m.group(2) for w in want) and i + 1 < len(lines):
        why = lines[i + 1].strip()
        print(f"  {m.group(2):28s} <- {why.rsplit('/', 1)[-1]}")
    i += 1
PY
echo "== S5a link probe done" | tee -a $OUT
