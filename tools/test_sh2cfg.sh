#!/bin/sh
# PORT32X: S5b - the SH2 configuration (no-draw + small arrays) against the normal engine.
#   tools/test_sh2cfg.sh                   synthetic stage, camera driven by the test
#   tools/test_sh2cfg.sh <Data.rsdk> [N]   the real Palmtree Panic 1, N frames (default 1800),
#                                          spin-dash pad as run_md_check.sh
# Both builds write a camera log (camera, layers, per-line scroll, palettes, ALL object state per
# frame); they must be identical. S5c: the SH2 configuration takes scripts, chunks and collision
# from the cart image the normal build wrote (as the console will from the cart). The SH2 configuration runs under AddressSanitizer: a write into
# a shrunk array stops it at that line. Must say "camera logs identical".
# Speed rounds: SAVE_REF=<file> before the change, REF=<file> after it - must also say "same as the reference".
set -e
cd "$(dirname "$0")/.."
make -s -C host
make -s -C host SH2CFG=1 ASAN=1
T=$(mktemp -d)
FRAMES=${2:-1800}
if [ -n "$1" ]; then
    DATA=$(readlink -f "$1")
    printf "[Dev]\nStartingCategory=1\nStartingScene=0\nDataFile=%s\n" "$DATA" > "$T/settings.ini"
    PAD="t3500:-"
    t=4000
    while [ $t -lt $((FRAMES * 1000 / 60)) ]; do
        PAD="$PAD,t$t:D,t$((t + 150)):D+C,t$((t + 300)):D,t$((t + 900)):R,t$((t + 2400)):R+C,t$((t + 2700)):R,t$((t + 3900)):-"
        t=$((t + 4000))
    done
    CAM=""
else
    FRAMES=${FRAMES:-600}
    FRAMES=600
    KEEP=1 FRAMES=10 ./tools/test_md_check.sh > /dev/null 2>&1 || true
    S=$(ls -dt /tmp/tmp.* | head -1)
    cp "$S/Data.rsdk" "$T/Data.rsdk"
    rm -rf "$S"
    printf "[Dev]\nStartingCategory=1\nStartingScene=0\nDataFile=Data.rsdk\n" > "$T/settings.ini"
    PAD=""
    CAM=1
fi
# the normal build writes the cart image (scripts, chunks, collision) after the stage load;
# the SH2 configuration reads them from it (S5c) - it runs second
for B in soniccd_host soniccd_host_sh2cfg_asan; do
    mkdir -p "$T/$B"
    cp "$T/settings.ini" "$T/$B/settings.ini"
    [ -f "$T/Data.rsdk" ] && cp "$T/Data.rsdk" "$T/$B/Data.rsdk"
    if [ "$B" = soniccd_host ]; then IMG_OUT="$T/cart.img"; IMG_IN=""; else IMG_OUT=""; IMG_IN="$T/cart.img"; fi
    (cd "$T/$B" && SCD32X_CARTIMG_OUT="$IMG_OUT" SCD32X_CARTIMG_IN="$IMG_IN" SCD32X_MDCHECK=1 SCD32X_TEST_CAMERA=$CAM SCD32X_CAMLOG=camlog.txt SCD32X_MAX_FRAMES=$FRAMES SCD32X_PAD="$PAD" \
        "$OLDPWD/host/$B" > out.txt 2>&1) || echo "$B exited with an error (see below)"
done
if grep -q "ERROR: AddressSanitizer" "$T/soniccd_host_sh2cfg_asan/out.txt"; then
    echo "SH2 configuration: AddressSanitizer error:"
    grep -A 12 "ERROR: AddressSanitizer" "$T/soniccd_host_sh2cfg_asan/out.txt" | head -16
    exit 1
fi
if grep -q "PORT32X:" "$T/soniccd_host_sh2cfg_asan/out.txt"; then
    echo "SH2 configuration reported (each once):"
    grep -o "PORT32X:.*" "$T/soniccd_host_sh2cfg_asan/out.txt" | sort -u
fi
# S5h: the chunk patch / collision overlay's own self-test (a short extra run of the SH2 configuration)
(cd "$T/soniccd_host_sh2cfg_asan" && SCD32X_CARTIMG_IN="$T/cart.img" SCD32X_MDCHECK=1 SCD32X_TEST_OVERLAY=1 SCD32X_MAX_FRAMES=5 \
    "$OLDPWD/host/soniccd_host_sh2cfg_asan" > selftest.txt 2>&1) || true
grep -h "overlay self-test" "$T/soniccd_host_sh2cfg_asan/selftest.txt" "$T/soniccd_host_sh2cfg_asan/"*.txt 2>/dev/null | sort -u | head -1
N1=$(wc -l < "$T/soniccd_host/camlog.txt")
# REV speed rounds: SAVE_REF=file keeps this run's log as the reference; REF=file compares this run against
# it (an engine change that must not change the game: same camera, scroll, palettes and object state)
[ -n "$SAVE_REF" ] && cp "$T/soniccd_host/camlog.txt" "$SAVE_REF" && echo "reference log saved: $SAVE_REF"
if [ -n "$REF" ]; then
    if cmp -s "$REF" "$T/soniccd_host/camlog.txt"; then
        echo "same as the reference $REF: $N1 frames"
    else
        echo "DIFFERENT from the reference $REF - first differences (reference < > this engine):"
        diff "$REF" "$T/soniccd_host/camlog.txt" | head -6
        echo "logs kept in $T"
        exit 1
    fi
fi
if cmp -s "$T/soniccd_host/camlog.txt" "$T/soniccd_host_sh2cfg_asan/camlog.txt"; then
    echo "camera logs identical: $N1 frames (camera, layer scroll, palettes, all object state; SH2 configuration from the cart image)"
    rm -rf "$T"
else
    echo "camera logs DIFFER - first differences (normal < > SH2 configuration):"
    diff "$T/soniccd_host/camlog.txt" "$T/soniccd_host_sh2cfg_asan/camlog.txt" | head -6
    echo "logs kept in $T"
    exit 1
fi
