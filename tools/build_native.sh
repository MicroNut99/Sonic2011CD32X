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
# R1V: the subs that run every frame - their code into SDRAM on the console (sh2emu -profile: "scripts per engine
# frame by object type and event" and the cart code stall ranking); names as the game names its object types
# R1X: and the shared instruction functions the frame's scripts call most (op:FUNC_...)
HOT=${SCD_HOT_SCRIPTS:-StageSetup:player,Ring:player,PlayerObject:draw,PlayerObject:main,Ring:draw,StageSetup:main,PaletteAnimation:main,HUD:draw,op:FUNC_PLAYEROBJECTCOLLISION,op:FUNC_DRAWNUMBERS,op:FUNC_DRAWSPRITE,op:FUNC_DRAWSPRITESCREENXY}
python3 tools/scd_aot.py --data "$DATA" --ii engine/Script_aot.ii --scene "$SCENE" --out engine/ScriptNative.inc --hot "$HOT" \
    --part global --ops engine/ScriptNative.ops   # REV R4: the global scripts only (the scenes carry their own: build_game_data.py --native)
rm -f engine/Script_aot.ii
