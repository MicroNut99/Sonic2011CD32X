#!/bin/bash
# SonicCD 32X - round S5b: PC host build, checks, plane model, 68000/SH2 demo on the PC, SH2 configuration check, SH2 compile.
# Usage: ./build.sh 2>&1 | tee build_log.txt
set -e
trap 'echo; echo "===== BUILD FAILED at line $LINENO: $BASH_COMMAND ====="' ERR
cd "$(dirname "$0")"
echo "== 1. patch check (engine/ = original decomp + patches/engine.patch)"
./tools/check_patch.sh
echo "== 2. host build"
make -s -j4 -C host
echo "== 3. pack reader check (must say 0 different)"
./tools/test_rsdk_pack.sh
echo "== 4. config / bytecode parser check (must say 0 different)"
./tools/test_scd_report.sh
echo "== 5. plane streaming stress test (md/md_planes.c alone, must say 0 pixels wrong)"
cc -std=c99 -O2 -Wall md/md_planes.c md/test_md_planes.c -o host/test_md_planes
./host/test_md_planes
echo "== 6. plane check on a synthetic stage (engine layers vs Mega Drive model, must say OK)"
./tools/test_md_check.sh
echo "== 7. the 68000 demo program on the PC against a VDP model (must say 0 different)"
./tools/test_mdplane.sh
echo "== 8. the SH2 configuration (no-draw, small arrays) against the normal engine (must say identical)"
./tools/test_sh2cfg.sh
echo "== 9. console cart image builder self-check (must say OK)"
g++ -std=c++17 -O2 -DSONICCD32X -w -I. tools/cartimg/cart_build.cpp -o host/cart_build
./host/cart_build --check | tail -1
gcc -std=c99 -O2 -Wall tools/cartimg/cart_verify_host.c md/cart_verify.c -o host/cart_verify_host
echo "== 9b. static memory report (biggest first)"
python3 tools/mem_report.py host/soniccd_host 16384 | head -n 24
echo "== 10. SH2 compile of the engine (compile only; full output in sh2_size.txt)"
./tools/sh2_size.sh | grep -E "TOTALS|^==|FAILED" || true
echo "== build OK"
