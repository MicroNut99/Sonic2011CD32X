#!/bin/bash
# PORT32X: round S1 - PC host build and checks. Run from the project folder:
#   ./build_host.sh 2>&1 | tee build_log.txt
# Builds host/soniccd_host (+ the ASan build), checks the engine patch, checks the
# Data.rsdk tool against the engine's own reader, and prints the static memory report.
set -e
trap 'echo; echo "=================== BUILD FAILED at line $LINENO: $BASH_COMMAND ==================="' ERR
cd "$(dirname "$0")"
echo "== 1. patch check"
./tools/check_patch.sh
echo "== 2. host build"
make -s -C host -j4
make -s -C host -j4 ASAN=1
echo "== 3. Data.rsdk tool vs engine reader (must say 0 different)"
./tools/test_rsdk_pack.sh
echo "== 4. static memory of the engine"
python3 tools/mem_report.py host/soniccd_host 16384
echo
echo "== S1 build OK"
