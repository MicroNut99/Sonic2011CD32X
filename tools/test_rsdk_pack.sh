#!/bin/sh
# PORT32X: builds a pack with tools/rsdk_pack.py and reads it back with the engine's
# own reader (host/test_reader). Must print "0 different". Run from the project root.
set -e
T=$(mktemp -d)
python3 - "$T" <<'PY'
import os, random, sys
sys.path.insert(0, "tools")
import rsdk_pack
t = sys.argv[1]
random.seed(1)
files = {}
for i, size in enumerate([0, 1, 7, 255, 508, 509, 4096, 8191, 8192, 20000, 70001]):
    files[f"Data/Test/A/file{i}.bin"] = bytes(random.randrange(256) for _ in range(size))
files["Data/Stages/R11A/16x16Tiles.gif"] = b"GIF89a" + bytes([16, 0, 0, 64]) + bytes(300)
files["Data/Game/GameConfig.bin"] = bytes(range(256)) * 3
for name, data in files.items():
    os.makedirs(os.path.join(t, "ref", os.path.dirname(name)), exist_ok=True)
    open(os.path.join(t, "ref", name), "wb").write(data)
    assert rsdk_pack.decrypt(rsdk_pack.encrypt(data)) == data
rsdk_pack.write_pack(os.path.join(t, "Data.rsdk"), files)
pk = rsdk_pack.Pack(os.path.join(t, "Data.rsdk"))
assert sorted(n for n, o, s in pk.files) == sorted(files)
for name in files:
    assert pk.read(name) == files[name], name
open(os.path.join(t, "names.txt"), "w").write(" ".join(sorted(files)))
print(f"python round trip OK ({len(files)} files)")
PY
make -s -C host test_reader
cd "$T" && "$OLDPWD/host/test_reader" "$T/Data.rsdk" "$T/ref" $(cat names.txt) | tail -n 4
rm -rf "$T"
