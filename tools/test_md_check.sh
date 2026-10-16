#!/bin/sh
# PORT32X: runs the host engine on a synthetic stage (4-line tileset, random chunks with
# flips and front/behind tiles, a 768-pixel 3-band parallax background with auto-scroll)
# while the camera follows a path with runs, reversals and jumps, and compares every frame's
# tile layers with the Mega Drive plane model. Must report 0 differences. Run from the root.
set -e
T=$(mktemp -d)
python3 - "$T" <<'PY'
import os, random, struct, sys, io, zlib
sys.path.insert(0, "tools")
import rsdk_pack as rp
t = sys.argv[1]
random.seed(11)
def s(x): b = x.encode(); return bytes([len(b)]) + b
# GameConfig: one regular stage R11A act 1, no objects, no sfx
gc = s("Sonic CD") + s("Data") + s("test") + bytes([0]) + bytes([0]) + bytes([0]) + bytes([0])
gc += bytes([0]) + bytes([1]) + s("R11A") + s("1") + s("PALMTREE PANIC") + bytes([0]) + bytes([0]) + bytes([0])
empty_bc = struct.pack("<i", 0) + struct.pack("<i", 0) + struct.pack("<H", 0) + struct.pack("<H", 0)
# tileset 16x16384: 4 groups of 15 palette indices; every 8x8 cell uses one group
groups = [list(range(1, 16)), list(range(20, 35)), list(range(96, 111)), list(range(130, 145))]
W, H = 16, 16384
pix = bytearray(W * H)
for tile in range(1, 1024):
    for q in range(4):
        if random.random() < 0.1: continue  # empty cell
        g = random.choice(groups)
        cols = random.sample(g, random.randint(1, 15))
        for y in range(8):
            for x in range(8):
                if random.random() < 0.85:
                    pix[(tile * 16 + (q >> 1) * 8 + y) * W + (q & 1) * 8 + x] = random.choice(cols)
def gif(w, h, data, pal):
    # LZW with clear codes every 250 symbols (always valid, simple)
    out, bits, nbits = bytearray(), 0, 0
    def emit(code, size):
        nonlocal bits, nbits
        bits |= code << nbits; nbits += size
        while nbits >= 8:
            out.append(bits & 255); bits >>= 8; nbits -= 8
    i = 0
    while i < len(data):
        emit(256, 9)
        for v in data[i:i + 250]: emit(v, 9)
        i += 250
    emit(257, 9)
    if nbits: out.append(bits & 255)
    blocks = b"".join(bytes([len(out[k:k + 255])]) + out[k:k + 255] for k in range(0, len(out), 255)) + b"\x00"
    return (b"GIF89a" + struct.pack("<HHBBB", w, h, 0xF7, 0, 0) + pal + b"," + struct.pack("<HHHHB", 0, 0, w, h, 0)
            + bytes([8]) + blocks + b";")
pal = bytes(random.randrange(256) for _ in range(768))
tiles_gif = gif(W, H, bytes(pix), pal)
# chunks: random tiles, flips, front/behind, a few never drawn (visualPlane 2)
ch = bytearray()
for c in range(512):
    for e in range(64):
        tile = 0 if c == 0 else random.randrange(1, 301)
        vp = random.choice([0, 0, 0, 1, 1, 2]) if c else 0
        ch += bytes([(vp << 4) | (random.randrange(4) << 2) | (tile >> 8), tile & 255, 0])
# act: layers BG1 (slot 0), FG (slots 1 and 2), midpoint 2; FG 40x8 chunks
fgw, fgh = 40, 8
act = s("TEST ZONE") + bytes([1, 0, 0, 9, 2, fgw, fgh]) + b"".join(struct.pack(">H", random.randrange(1, 512)) for _ in range(fgw * fgh)) + bytes([0]) + struct.pack(">H", 0)
# background: 1 layer 6x2 chunks (768 px, wider than the plane), 3 parallax bands on 8-line edges
bw, bh = 6, 2
hpar = [(0x40, 0, 0), (0x80, 3, 0), (0x100, 0, 0)]
bg = bytes([1, len(hpar)]) + b"".join(struct.pack(">HBB", f, sp, d) for f, sp, d in hpar) + bytes([0])
bg += bytes([bw, bh, 1]) + struct.pack(">H", 0x80) + bytes([0])
line = []
for y in range(bh * 128):
    line.append(0 if y < 88 else 1 if y < 168 else 2)
rle = bytearray()
k = 0
while k < len(line):
    n = 1
    while k + n < len(line) and line[k + n] == line[k] and n < 250: n += 1
    rle += bytes([0xFF, line[k], n + 1]) if n > 1 else bytes([line[k]])
    k += n
bg += bytes(rle) + b"\xff\xff" + b"".join(struct.pack(">H", random.randrange(1, 512)) for _ in range(bw * bh))
# S7a4: two stage sound-effect names - the save / bytecode / reopen / name loop of LoadStageFiles runs
sfxnames = [b"Ring.wav", b"Jump.wav"]
sc = bytes([1]) + bytes(random.randrange(256) for _ in range(96)) + bytes([0]) + bytes([len(sfxnames)]) + b"".join(bytes([len(n)]) + n for n in sfxnames)
files = {"Data/Game/GameConfig.bin": gc, "Data/Scripts/ByteCode/GS000.bin": empty_bc, "Data/Scripts/ByteCode/RS000.bin": empty_bc,
         "Data/Palettes/MasterPalette.act": bytes(random.randrange(256) for _ in range(768)),
         "Data/Stages/R11A/16x16Tiles.gif": tiles_gif, "Data/Stages/R11A/128x128Tiles.bin": bytes(ch),
         "Data/Stages/R11A/Act1.bin": act, "Data/Stages/R11A/Backgrounds.bin": bg, "Data/Stages/R11A/StageConfig.bin": sc}
rp.write_pack(os.path.join(t, "Data.rsdk"), files)
open(os.path.join(t, "settings.ini"), "w").write("[Dev]\nStartingCategory=1\nStartingScene=0\nDataFile=Data.rsdk\n")
PY
make -s -C host
(cd "$T" && SCD32X_MDEXPORT="$MDEXPORT" SCD32X_MDEXPORT_START=${MDEXPORT_START:-50} SCD32X_MDCHECK=1 SCD32X_TEST_CAMERA=1 SCD32X_MAX_FRAMES=${FRAMES:-600} SCD32X_EVENT_LOG=events.txt "$OLDPWD/host/${HOSTBIN:-soniccd_host}" > out.txt 2>&1 || true)
grep -E "MDCHECK|MDEXPORT" "$T/events.txt" | grep -v "MDCHECK frame " | sed 's/^\[ *[0-9]*\] //'
N=$(grep -c "MDCHECK summary: .* 0 with differences" "$T/events.txt" || true)
C=$(sed -n 's/.*frames with layers, \([0-9]*\) compared.*/\1/p' "$T/events.txt")
[ "$KEEP" = 1 ] && echo "kept $T" || rm -rf "$T"
if [ "$N" = 1 ] && [ "${C:-0}" -gt 100 ]; then echo "plane check: OK"; else echo "plane check: FAILED"; exit 1; fi
