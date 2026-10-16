#!/bin/sh
# Makes engine/ = the original RSDKv3 decompilation + patches/engine.patch.
# Run from the project root. Argument: path to the original RSDKv3 folder
# (default RSDKv3-Decompilation-master/RSDKv3, i.e. the GitHub zip unpacked here).
ORIG=${1:-RSDKv3-Decompilation-master/RSDKv3}
[ -d "$ORIG" ] || { echo "original source not found: $ORIG"; exit 1; }
[ -e engine ] && { echo "engine/ already exists - remove it first"; exit 1; }
cp -r "$ORIG" engine
rm -f engine/*.vcxproj* engine/*.rc engine/*.ico engine/resource.h
patch -s -p1 --directory=engine < patches/engine.patch || { echo "PATCH FAILED"; exit 1; }
echo "engine/ ready (original + patches/engine.patch)"
