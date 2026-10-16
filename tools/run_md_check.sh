#!/bin/sh
# PORT32X: runs a real stage in the host engine and checks its tile layers against the Mega
# Drive plane model (round S3a).
#   tools/run_md_check.sh <Data.rsdk> [scene number in the regular list, 0 = R11A] [frames]
# Results in runs/scene<N>/: events.txt (MDCHECK lines), frames/ (one frame per second and the
# first frames that differ: reference | model | difference), MDPLANE.dat (the recording for the
# 68000 demo, from frame MDEXPORT_START, default 600). ASAN=1 uses the AddressSanitizer build.
set -e
DATA=$(readlink -f "$1")
SCENE=${2:-0}
FRAMES=${3:-3600}
[ -f "$DATA" ] || { echo "data pack not found: $1"; exit 1; }
cd "$(dirname "$0")/.."
BIN=soniccd_host
if [ "$ASAN" = 1 ]; then make -s -C host ASAN=1; BIN=soniccd_host_asan; else make -s -C host; fi
R=runs/scene$SCENE
rm -rf "$R" && mkdir -p "$R/frames"
printf "[Dev]\nStartingCategory=1\nStartingScene=%s\nDataFile=%s\n" "$SCENE" "$DATA" > "$R/settings.ini"
# pad: wait for the title card, then repeat: spin dash (hold down, press jump, release down while
# holding right), run right for 3 s with one jump - this gets Sonic up slopes and through loops
PAD="t3500:-"
t=4000
while [ $t -lt $((FRAMES * 1000 / 60)) ]; do
    PAD="$PAD,t$t:D,t$((t + 150)):D+C,t$((t + 300)):D,t$((t + 900)):R,t$((t + 2400)):R+C,t$((t + 2700)):R,t$((t + 3900)):-"
    t=$((t + 4000))
done
cd "$R"
SCD32X_MDEXPORT=$PWD/MDPLANE.dat SCD32X_MDEXPORT_START=${MDEXPORT_START:-600} SCD32X_MDCHECK=1 SCD32X_MAX_FRAMES=$FRAMES SCD32X_PAD="$PAD" SCD32X_FRAMES_DIR=$PWD/frames SCD32X_SAVE_EVERY=60 \
    SCD32X_EVENT_LOG=events.txt "../../host/$BIN" > out.txt 2>&1 || echo "host exited with an error (see $R/out.txt)"
grep -E "MDCHECK|MDEXPORT|END|PORT32X" events.txt | grep -v "MDCHECK frame " || true
echo "frames that differ (first 10):"
grep "MDCHECK frame " events.txt | head -10 || true
cd ../..
if [ -f "$R/MDPLANE.dat" ]; then
    echo "the 68000 demo program on the PC, playing this recording (must say 0 different):"
    cc -std=c99 -O2 -Wall -DMDPLANE_HOST -Imd md/mdplane_main.c md/sh2_frames.c md/md_record.c md/md_planes.c host/test_mdplane.c -o host/test_mdplane
    ./host/test_mdplane "$R/MDPLANE.dat" || true
fi
echo "results in $R"
