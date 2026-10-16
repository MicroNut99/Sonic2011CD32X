#!/bin/sh
# PORT32X: proves engine/ = the original RSDKv3 decomp + patches/engine.patch.
# Run from the project root. Optional argument: path to the original RSDKv3 folder.
ORIG=${1:-RSDKv3-Decompilation-master/RSDKv3}
[ -d "$ORIG" ] || { echo "original source not found: $ORIG"; exit 1; }
T=$(mktemp -d)
cp -r "$ORIG" "$T/engine"
rm -f "$T"/engine/*.vcxproj* "$T"/engine/*.rc "$T"/engine/*.ico "$T"/engine/resource.h
(cd "$T" && patch -s -p1 --directory=engine < "$OLDPWD/patches/engine.patch") || { echo "PATCH FAILED"; rm -rf "$T"; exit 1; }
# R1U: engine/ScriptNative.inc is made by tools/build_native.sh from the game's data, not by the patch
if diff -r -x obj -x 'obj_*' -x ScriptNative.inc -x Script_aot.ii "$T/engine" engine > /dev/null; then echo "patch check: OK (engine/ = original + patches/engine.patch)"; R=0
else echo "patch check: DIFFERENT"; diff -rq -x ScriptNative.inc -x Script_aot.ii "$T/engine" engine; R=1; fi
rm -rf "$T"; exit $R
