#!/usr/bin/env python3
"""PORT32X: S5d - which script functions every stage's bytecode uses.
  tools/scan_bytecode.py Data.rsdk [report.md]
Decodes each bytecode file as LoadBytecode does and walks its instructions as ProcessScript reads
them (function number, then opcodeSize operands: 1 = variable [array mode, +2 words unless mode
0, variable number], 2 = integer, 3 = string [length, 4 chars per word, closing word]). The
function table (names, operand counts) is read from engine/Script.cpp, so it cannot drift from the
interpreter. Every file must walk exactly to its end ("walk OK") - that proves the decoding.
Reports, per function the SH2 build cares about, the stages whose scripts use it."""
import os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk_pack as rp
import scd_report as sr

WATCH = {
    "Set16x16TileInfo": "writes the chunk / collision tables at run time (cart data on the SH2)",
    "Copy16x16Tile": "animated tiles (Mega Drive tile updates later)",
    "SetTileLayerEntry": "writes a layout (layout pool guard)",
    "GetTileLayerEntry": "reads a layout (layout pool guard)",
    "Draw3DScene": "3D scene (3D buffers)",
    "MatrixMultiply": "3D scene (3D buffers)",
    "TransformVertices": "3D scene (3D buffers)",
    "LoadAnimation": "loads an animation file",
    "SpriteFrame": "adds sprite frames",
    "LoadSpriteSheet": "loads a sprite sheet",
    "LoadPalette": "loads a palette",
    "LoadVideo": "plays a video",
    "PlayMusic": "music",
}


def function_table(script_cpp):
    src = open(script_cpp, errors="replace").read()
    i = src.index("const FunctionInfo functions[]") if "const FunctionInfo functions[]" in src else src.index("FunctionInfo functions[]")
    j = src.index("};", i)
    return [(m.group(1), int(m.group(2))) for m in re.finditer(r'FunctionInfo\("([^"]+)",\s*(\d+)\)', src[i:j])]


VAR_GROUPS = {
    "3D buffers (VAR_3DSCENE*, VAR_VERTEXBUFFER*, VAR_FACEBUFFER*)": ("VAR_3DSCENE", "VAR_VERTEXBUFFER", "VAR_FACEBUFFER"),
    "layer size (VAR_TILELAYERXSIZE / YSIZE)": ("VAR_TILELAYERXSIZE", "VAR_TILELAYERYSIZE"),
}


def variable_table(script_cpp):
    src = open(script_cpp, errors="replace").read()
    i = src.index("enum ScrVariable {")
    j = src.index("};", i)
    return re.findall(r"\b(VAR_[A-Z0-9_]+)\b", src[i:j])


def decode_ints(d):
    """the script code block of a bytecode file -> list of int32 (as LoadBytecode)."""
    (count,) = struct.unpack_from("<i", d, 0)
    p, out = 4, []
    while count > 0:
        b = d[p]
        p += 1
        n = b & 0x7F
        for _ in range(n):
            if b >= 0x80:
                out.append(struct.unpack_from("<i", d, p)[0])
                p += 4
            else:
                out.append(d[p])
                p += 1
        count -= n
    return out


def walk(code, funcs):
    """-> (counts per function index, ok, error text)"""
    counts, varsUsed = {}, set()
    p, n = 0, len(code)
    while p < n:
        op = code[p]
        p += 1
        if op < 0 or op >= len(funcs):
            return counts, varsUsed, False, f"unknown function {op} at word {p - 1}"
        counts[op] = counts.get(op, 0) + 1
        for _ in range(funcs[op][1]):
            if p >= n:
                return counts, varsUsed, False, "ran past the end inside an instruction"
            t = code[p]
            p += 1
            if t == 1:
                mode = code[p]
                p += 1
                if mode != 0:
                    p += 2
                varsUsed.add(code[p])
                p += 1
            elif t == 2:
                p += 1
            elif t == 3:
                ln = code[p]
                p += 1
                p += ln // 4 + 1
            else:
                return counts, varsUsed, False, f"unknown operand type {t} at word {p - 1}"
    return counts, varsUsed, p == n, "" if p == n else "ran past the end"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    here = os.path.dirname(os.path.abspath(__file__))
    funcs = function_table(os.path.join(here, "..", "engine", "Script.cpp"))
    varNames = variable_table(os.path.join(here, "..", "engine", "Script.cpp"))
    data = sr.Data(sys.argv[1])
    gc = sr.parse_gameconfig(data.read("Data/Game/GameConfig.bin"))
    mobile = data.has("Data/Scripts/ByteCode/GlobalCode.bin")
    files = []
    if mobile:
        files.append(("GLOBAL", "Data/Scripts/ByteCode/GlobalCode.bin"))
    else:
        files.append(("GLOBAL", "Data/Scripts/ByteCode/GS000.bin"))
    for cat in range(4):
        for pos, (folder, sid, name) in enumerate(gc["lists"].get(cat, [])):
            bc = f"Data/Scripts/ByteCode/{folder}.bin" if mobile else f"Data/Scripts/ByteCode/{'PRBS'[cat]}S{pos:03d}.bin"
            if data.has(bc):
                files.append((f"{folder}/{sid}", bc))
    out = open(sys.argv[2], "w") if len(sys.argv) > 2 else sys.stdout
    w = out.write
    w(f"# Bytecode scan\n\n{len(funcs)} functions in the engine's table; {len(files)} bytecode files.\n\n")
    users = {name: [] for name in WATCH}
    groupUsers = {g: [] for g in VAR_GROUPS}
    bad = 0
    for label, path in files:
        code = decode_ints(data.read(path))
        counts, varsUsed, ok, err = walk(code, funcs)
        names = {varNames[v] for v in varsUsed if 0 <= v < len(varNames)}
        for g, prefixes in VAR_GROUPS.items():
            if any(nm.startswith(prefixes) for nm in names):
                groupUsers[g].append(label)
        if not ok:
            bad += 1
            w(f"- {label}: walk FAILED ({err})\n")
        for idx, c in counts.items():
            name = funcs[idx][0]
            if name in users:
                users[name].append(f"{label} ({c})")
    w(f"\nWalk: {len(files) - bad} of {len(files)} files OK" + (" - all decoded exactly to their end.\n" if not bad else " - see failures above.\n"))
    w("\n| Function | Why it matters | Used by (calls in the file) |\n|---|---|---|\n")
    for name, why in WATCH.items():
        u = users[name]
        w(f"| {name} | {why} | {', '.join(u) if u else '- (nowhere)'} |\n")
    w("\n| Script variables | Used by |\n|---|---|\n")
    for g, u in groupUsers.items():
        w(f"| {g} | {', '.join(u) if u else '- (nowhere)'} |\n")
    if out is not sys.stdout:
        out.close()
        print(f"report written to {sys.argv[2]}" + (f" ({bad} files failed to walk)" if bad else " (all files walked OK)"))


if __name__ == "__main__":
    main()
