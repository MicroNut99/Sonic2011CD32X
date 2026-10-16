#!/bin/bash
# PORT32X: first SH2 compile of the engine (compile only, no link yet).
# Prints the code size per file at -O2 and -Os, the biggest functions, and every file
# sh-elf-g++ rejects (errors in build_sh2/<opt>/errors.txt). Writes sh2_size.txt.
# Overridable for testing: SH (tool prefix), ARCH (CPU flags).
SH=${SH-/opt/toolchains/sega/sh-elf/bin/sh-elf-}
ARCH=${ARCH--m2 -mb}
FLAGS="-std=c++17 -DSONICCD32X $ARCH -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -w"
cd "$(dirname "$0")/.."
OUT=sh2_size.txt
: > $OUT
if ! command -v ${SH}g++ > /dev/null; then
    echo "== ${SH}g++ not found - SH2 compile skipped" | tee -a $OUT
    exit 0
fi
FAILED=""
for OPT in O2 Os; do
    D=build_sh2/$OPT
    rm -rf $D && mkdir -p $D
    for f in engine/*.cpp; do
        o=$D/$(basename $f .cpp).o
        if ! ${SH}g++ $FLAGS -$OPT -c $f -o $o 2>> $D/errors.txt; then
            FAILED="$FAILED $OPT:$(basename $f)"
        fi
    done
    echo "== code and data per file at -$OPT (text = code, bss = zeroed arrays)" | tee -a $OUT
    ${SH}size -t $D/*.o 2>/dev/null | sed "s#$D/##" | tee -a $OUT
done
echo "== 25 biggest functions at -O2 (bytes)" | tee -a $OUT
${SH}nm -C -S build_sh2/O2/*.o 2>/dev/null | grep -i " t " | \
    python3 -c 'import sys
rows = []
for l in sys.stdin:
    p = l.split(None, 3)
    if len(p) == 4:
        rows.append((int(p[1], 16), p[3].strip()))
for size, name in sorted(rows, reverse=True)[:25]:
    print(f"{size:8d}  {name}")' | tee -a $OUT
if [ -n "$FAILED" ]; then
    echo "== FAILED to compile:$FAILED" | tee -a $OUT
    echo "== first errors:" | tee -a $OUT
    grep -h -m 20 "error" build_sh2/*/errors.txt | head -20 | tee -a $OUT
else
    echo "== all engine files compiled for the SH2" | tee -a $OUT
fi
