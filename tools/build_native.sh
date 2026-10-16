#!/bin/sh
# PORT32X R1U: engine/ScriptNative.inc - the scene's scripts as native code (tools/scd_aot.py).
#   tools/build_native.sh <Data.rsdk> [list:pos]      (default 1:0, Palmtree Panic 1)
# Run after tools/setup_engine.sh (which rebuilds engine/ without it). Without the file the engine
# builds and runs exactly as before: every script interpreted.
set -e
DATA=$(readlink -f "$1")
SCENE=${2:-1:0}
cd "$(dirname "$0")/.."
ROOT=${GENDEV:-/opt/toolchains/sega}
CXX=$ROOT/sh-elf/bin/sh-elf-g++
[ -x "$CXX" ] || { echo "no $CXX"; exit 1; }
rm -f engine/ScriptNative.inc
# the interpreter as the SH2 build compiles it (the same flags as cd32x/Makefile's ENGFLAGS)
$CXX -E -P -std=c++17 -DSONICCD32X -DSCD32X_SH2 -DSCD_PLANES=1 -DSCD_AOT_EXTRACT -m2 -mb -Icd32x/kobo/sh2 \
    engine/Script.cpp -o engine/Script_aot.ii
python3 tools/scd_aot.py --data "$DATA" --ii engine/Script_aot.ii --scene "$SCENE" --out engine/ScriptNative.inc
rm -f engine/Script_aot.ii
