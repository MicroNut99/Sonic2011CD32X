#!/bin/sh
# PORT32X: checks tools/scd_report.py against the engine: GameConfig stage lists and
# global sound effects (LoadGameConfig / LoadGlobalSfx) and bytecode sizes (LoadBytecode).
# Must print "0 different". Run from the project root.
set -e
T=$(mktemp -d)
python3 - "$T" <<'PY'
import os, random, struct, sys
sys.path.insert(0, "tools")
import rsdk_pack as rp, scd_report as sr
t = sys.argv[1]
random.seed(4)
def s(x): b = x.encode(); return bytes([len(b)]) + b
def wav(rate, bits, n):
    data = bytes(random.randrange(256) for _ in range(n * bits // 8))
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * bits // 8, bits // 8, bits)
    return b"RIFF" + struct.pack("<I", 4 + 8 + 16 + 8 + len(data)) + b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data)) + data
gc = s("Sonic CD") + s("Data") + s("A test config")
objs = ["Ring", "Spring", "Monitor"]
gc += bytes([len(objs)]) + b"".join(s(o) for o in objs) + b"".join(s("Global/" + o + ".txt") for o in objs)
gc += bytes([2]) + s("Options.Soundtrack") + struct.pack(">i", 1) + s("Player.Score") + struct.pack(">i", 0)
sfx = ["Global/Jump.wav", "Global/Ring.wav", "Global/Spring.wav"]
gc += bytes([len(sfx)]) + b"".join(s(x) for x in sfx)
gc += bytes([2]) + s("SONIC") + s("TAILS")
lists = [[("Title", "1", "TITLE")], [("R11A", "1", "PALMTREE PANIC 1"), ("R11B", "1", "PALMTREE PANIC 1 PAST"), ("R12A", "2", "PALMTREE PANIC 2")],
         [("SS1", "1", "SPECIAL 1"), ("SS2", "2", "SPECIAL 2")], [("Bonus", "1", "BONUS")]]
for lst in lists:
    gc += bytes([len(lst)]) + b"".join(s(f) + s(i) + s(n) + bytes([0]) for f, i, n in lst)
def block(values):
    out = struct.pack("<i", len(values)); i = 0
    while i < len(values):
        big = values[i] > 255 or values[i] < 0
        n = 1
        while i + n < len(values) and n < 127 and ((values[i + n] > 255 or values[i + n] < 0) == big): n += 1
        out += bytes([n | (0x80 if big else 0)])
        for v in values[i:i + n]: out += struct.pack("<i", v) if big else bytes([v])
        i += n
    return out
code = [random.choice([random.randrange(256), random.randrange(-100000, 100000)]) for _ in range(3000)]
jumps = [random.randrange(70000) for _ in range(150)]
# after the two tables: object scripts (u16 count, 4 code + 4 jump pointers each) and functions
tail = struct.pack("<H", 3) + bytes(3 * 32) + struct.pack("<H", 5) + bytes(5 * 8)
files = {"Data/Game/GameConfig.bin": gc, "Data/Scripts/ByteCode/GS000.bin": block(code) + block(jumps) + tail,
         "Data/SoundFX/Global/Jump.wav": wav(44100, 8, 5000), "Data/SoundFX/Global/Ring.wav": wav(44100, 16, 3333),
         "Data/SoundFX/Global/Spring.wav": wav(22050, 8, 777)}
rp.write_pack(os.path.join(t, "Data.rsdk"), files)
d = sr.Data(os.path.join(t, "Data.rsdk"))
g = sr.parse_gameconfig(d.read("Data/Game/GameConfig.bin"))
lines = []
for cat in range(4):
    for pos, (folder, sid, name) in enumerate(g["lists"].get(cat, [])):
        lines.append(f"STAGE {cat} {pos} {folder} {sid}")
for n in g["sfx"]:
    lines.append(f"SFX {n} {sr.wav_samples(d, 'Data/SoundFX/' + n)[0]}")
c, j = sr.bytecode_counts(d.read("Data/Scripts/ByteCode/GS000.bin"))
lines.append(f"BYTECODE {c} {j}")
open(os.path.join(t, "python.txt"), "w").write("\n".join(lines) + "\n")
PY
make -s -C host test_config
(cd "$T" && "$OLDPWD/host/test_config" "$T/Data.rsdk" > engine.txt)
N=$(diff "$T/python.txt" "$T/engine.txt" | grep -c '^[<>]' || true)
echo "config/bytecode lines compared: $(wc -l < "$T/engine.txt"), $N different"
[ "$N" = 0 ] || diff "$T/python.txt" "$T/engine.txt"
rm -rf "$T"
[ "$N" = 0 ]
