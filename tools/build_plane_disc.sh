#!/bin/sh
# PORT32X: round S3b - the plane demo disc from a real Palmtree Panic run.
#   tools/build_plane_disc.sh <Data.rsdk> [frames, default 1800]
# 1. tools/run_md_check.sh: real run, plane check, recording from frame 600, the 68000 program on
#    the PC (must say 0 different)
# 2. make -C cd32x: SH2 + Sub-CPU + Genesis programs, MDPLANE.BIN, the disc (cd32x/build/)
set -e
cd "$(dirname "$0")/.."
./tools/run_md_check.sh "$1" 0 "${2:-1800}"
make -C cd32x DATA=../runs/scene0/MDPLANE.dat
