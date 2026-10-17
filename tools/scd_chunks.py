#!/usr/bin/env python3
"""PORT32X REV R4B: the chunks a scene's scripts can put into its layouts at run time (SetTileLayerEntry).

  tools/scd_chunks.py <Data.rsdk> <list> <pos>    -> one line: "<constants> <types>"

A script that changes a layout entry places a chunk the act's starting layout may not have (Palmtree Panic's tile
triggers swap the ramps' chunks c..c+3 for c+4..c+7 when the player runs back through them; Wacky Workbench's tile
swapper, Stardust Speedway's path swap ...). The act file (host/md_check.cpp) holds Genesis cells only for the tiles
of the chunks it converts, so those chunks are converted too:
  - constants: every chunk number written as a constant (SetTileLayerEntry's first operand), as "layer:chunk,...";
  - types: the object types that write a computed chunk number, as "type,..." - md_check adds, around each object of
    those types, the chunks c .. c + 15 of the layout chunks next to it (a trigger's other half, an animation's frames).
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scd_report as sr
import scd_aot as aot


def placed(data_path, lst, pos):
    data = sr.Data(data_path)
    sc = aot.Scene(data, lst, pos)
    script_cpp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RSDKv3-Decompilation-master", "RSDKv3",
                              "Script.cpp")
    funcs, fenum, venum = aot.engine_tables(script_cpp)
    set_op = [i for i, (n, _) in enumerate(funcs) if n == "SetTileLayerEntry"][0]
    get_op = [i for i, (n, _) in enumerate(funcs) if n == "GetTileLayerEntry"][0]
    # each object type's subs: decode from each sub's start to its End (the subs are laid out one after another)
    starts = sorted({p for subs in sc.subs.values() for (p, j) in subs if p >= 0} | {p for p in sc.fcode if p >= 0})
    owner = {}
    for t, subs in sc.subs.items():
        for (p, j) in subs:
            if p >= 0:
                owner.setdefault(p, set()).add(t)
    consts, types = set(), set()
    for i, s in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else len(sc.code)
        p = s
        copied = set()  # variables that hold a layout entry read just now, unchanged: writing one back copies a chunk
        while p < end:
            try:
                op = aot.decode(sc.code, funcs, p)
            except (ValueError, IndexError):
                break
            a0 = op.args[0] if op.args else None
            if op.func == set_op and len(op.args) >= 2:
                a1 = op.args[1]
                layer = a1[1] if a1[0] == "int" else -1
                if a0[0] == "int":
                    consts.add((layer, a0[1]))
                elif a0 not in copied:  # (the sign post copies the layout's own chunks: nothing new)
                    types |= owner.get(s, set()) or {-1}  # a function's: no owner known
            elif op.func == get_op and a0 and a0[0] == "var":
                copied.add(a0)
            elif a0 is not None:
                copied.discard(a0)  # (any other instruction may write its first operand)
            p = op.next
    return consts, types


if __name__ == "__main__":
    consts, types = placed(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]))
    c = ",".join("%d:%d" % (l, ch) for (l, ch) in sorted(consts) if l >= 0) or "-"
    t = ",".join(str(x) for x in sorted(types) if x >= 0) or "-"
    print(c, t)
