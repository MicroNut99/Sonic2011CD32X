#!/bin/sh
# PORT32X: the Genesis demo program's C code on the PC against a VDP model, both modes:
#   format 2 (S3c): every frame precomputed on the PC, the 68000 copies
#   format 3 (S4):  the SH2 computes live (md/sh2_frames.c run as the SH2 would), hands each
#                   record over in the 32X frame buffer (COMM8 / COMM10 / FM)
# Each must say 0 different (and 0 protocol violations for format 3).
set -e
cd "$(dirname "$0")/.."
cc -std=c99 -O2 -Wall -DMDPLANE_HOST -Imd md/mdplane_main.c md/sh2_frames.c md/md_record.c md/md_planes.c host/test_mdplane.c -o host/test_mdplane
for F in 2 3; do
    T=$(mktemp -d)
    SCD32X_MDEXPORT_FORMAT=$F MDEXPORT="$T/MDPLANE.dat" MDEXPORT_START=50 FRAMES=${FRAMES:-500} ./tools/test_md_check.sh | grep -E "MDEXPORT wrote|FAILED" || true
    printf "format %s: " $F
    ./host/test_mdplane "$T/MDPLANE.dat"
    rm -rf "$T"
done
