#!/usr/bin/env python3
"""PORT32X R1U: native scripts - the game's script bytecode translated to C++ at build time.

  tools/scd_aot.py --data Data.rsdk --ii Script.ii --scene 1:0 --out native.inc [--only TYPE:SUB,...] [--report]

The engine (patches/engine.patch, Script.cpp) includes the output at the end of Script.cpp when it is built
with SCD_NATIVE_SCRIPTS. ProcessScript then runs the native version of an object's script sub instead of
interpreting it - when, and only when, the scene's loaded bytecode is exactly the bytecode translated here
(a hash over the script code, the jump table and the function table, checked once per stage load).

How a native instruction is made: the C++ statements the interpreter itself runs for that instruction,
taken from ProcessScript (tools/scd_aot_cpp.py, from `g++ -E` of the same Script.cpp with the build's
flags), in the interpreter's order -
  1. each operand read into scriptEng.operands[i] (array index first, then the variable's read code)
  2. the instruction's own code (its `case FUNC_...:` body in do { } while (0), opcodeSize as it starts)
  3. if opcodeSize is still > 0: each variable operand written back, array index computed again.
Only the dispatch is gone: no switch on the instruction, no switch on each variable, no bytecode reads.
Control flow (if / while / switch / loop / break / else / endif / CallFunction / EndFunction / End) becomes
gotos and calls, resolved here from the jump table exactly as the interpreter's jump-table stack would at
run time; a sub whose jump-table stack is not the same on every path to an instruction is not translated
(it stays interpreted), nor is one using an instruction or variable that needs the interpreter's own
position (3D vertex / face buffers)."""
import argparse, os, re, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scd_report as sr
import scd_aot_cpp as cpp

SUBS = ("main", "player", "draw", "setup")  # SUB_MAIN .. SUB_SETUP: objectScriptList's subMain,
SUB_FIELDS = ("subMain", "subPlayerInteraction", "subDraw", "subStartup")  # in ScriptPtr order


# ------------------------------------------------------------------ the engine's tables (from Script.cpp)
def engine_tables(script_cpp):
    src = open(script_cpp, errors="replace").read()
    i = src.index("FunctionInfo functions[]")
    j = src.index("};", i)
    funcs = [(m.group(1), int(m.group(2))) for m in re.finditer(r'FunctionInfo\("([^"]+)",\s*(\d+)\)', src[i:j])]
    i = src.index("enum ScrFunction {")
    j = src.index("};", i)
    fenum = re.findall(r"\b(FUNC_[A-Z0-9_]+)\b", src[i:j])
    i = src.index("enum ScrVariable {")
    j = src.index("};", i)
    venum = re.findall(r"\b(VAR_[A-Z0-9_]+)\b", src[i:j])
    # REV R4: how many of them are numbered the same in every build (before the first #if inside the enum)
    k = src.find("#", i, j)
    global VENUM_FIXED
    VENUM_FIXED = len(re.findall(r"\b(VAR_[A-Z0-9_]+)\b", src[i:k if k >= 0 else j]))
    assert len(fenum) >= len(funcs), (len(fenum), len(funcs))
    return funcs, fenum, venum


VENUM_FIXED = 0


# ------------------------------------------------------------------ the game data
def rd_str(d, p):
    n = d[p]
    return d[p + 1:p + 1 + n].decode("latin1"), p + 1 + n


def global_objects(gc):
    p = 0
    for _ in range(3):
        _, p = rd_str(gc, p)
    n = gc[p]
    p += 1
    names = []
    for _ in range(n):
        s, p = rd_str(gc, p)
        names.append(s)
    return names


def stage_objects(sc):
    p = 1 + 96
    n = sc[p]
    p += 1
    names = []
    for _ in range(n):
        s, p = rd_str(sc, p)
        names.append(s)
    return names


def decode_block(d, p):
    """LoadBytecode's packed int block -> (ints, new position)"""
    (count,) = struct.unpack_from("<i", d, p)
    p += 4
    out = []
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
    return out, p


def load_bytecode(d):
    """-> code, jump table, [(main, player, draw, startup) code ptrs], [same jt ptrs], [function code], [function jt]"""
    code, p = decode_block(d, 0)
    jt, p = decode_block(d, p)
    (ns,) = struct.unpack_from("<H", d, p)
    p += 2
    cp = [struct.unpack_from("<4i", d, p + 16 * s) for s in range(ns)]
    p += 16 * ns
    jp = [struct.unpack_from("<4i", d, p + 16 * s) for s in range(ns)]
    p += 16 * ns
    (nf,) = struct.unpack_from("<H", d, p)
    p += 2
    fc = list(struct.unpack_from("<%di" % nf, d, p))
    p += 4 * nf
    fj = list(struct.unpack_from("<%di" % nf, d, p))
    return code, jt, cp, jp, fc, fj


class Scene:
    """the scene's scripts as the engine has them after LoadStageFiles: global bytecode first, then the stage's"""

    def __init__(self, data, lst, pos):
        gc = data.read("Data/Game/GameConfig.bin")
        cfg = sr.parse_gameconfig(gc)
        folder, sid, name = cfg["lists"][lst][pos]
        self.label = "%s%s" % (folder, sid)
        sc = data.read("Data/Stages/%s/StageConfig.bin" % folder)
        gnames, snames = global_objects(gc), stage_objects(sc)
        self.names = ["Blank Object"] + gnames + snames
        self.code, self.jt = [], []
        self.subs = {}  # type -> [(code ptr, jt ptr)] x 4
        self.fcode, self.fjt = [], []
        files = []
        if sc[0]:  # the stage loads the global scripts
            files.append(("Data/Scripts/ByteCode/GS000.bin", 1))
        files.append(("Data/Scripts/ByteCode/%sS%03d.bin" % ("PRBS"[lst], pos), 1 + (len(gnames) if sc[0] else 0)))
        for path, first in files:
            c, j, cp, jp, fc, fj = load_bytecode(data.read(path))
            self.code += c  # LoadBytecode appends at scriptCodePos / jumpTablePos
            self.jt += j
            for s in range(len(cp)):
                self.subs[first + s] = list(zip(cp[s], jp[s]))
            self.fcode, self.fjt = fc, fj  # each file's function table replaces the list from index 0
        self.scene = (lst, pos)


# ------------------------------------------------------------------ one instruction, decoded as ProcessScript reads it
class Op:
    __slots__ = ("pos", "func", "args", "next")


def decode(code, funcs, p):
    op = Op()
    op.pos, op.func, op.args = p, code[p], []
    if not (0 <= op.func < len(funcs)):
        raise ValueError("unknown instruction %d at %d" % (op.func, p))
    q = p + 1
    for _ in range(funcs[op.func][1]):
        t = code[q]
        q += 1
        if t == 1:  # SCRIPTVAR_VAR
            mode = code[q]
            q += 1
            a = b = 0
            if mode in (1, 2, 3):
                a, b = code[q], code[q + 1]
                q += 2
            var = code[q]
            q += 1
            op.args.append(("var", mode, a, b, var))
        elif t == 2:  # SCRIPTVAR_INTCONST
            op.args.append(("int", code[q]))
            q += 1
        elif t == 3:  # SCRIPTVAR_STRCONST: 4 characters per int, the high byte first
            n = code[q]
            q += 1
            s = []
            for c in range(n):
                w = code[q] & 0xFFFFFFFF
                s.append((w >> (24 - 8 * (c % 4))) & 0xFF)
                if c % 4 == 3:
                    q += 1
            q += 1
            op.args.append(("str", bytes(s)))
        else:
            raise ValueError("unknown operand type %d at %d" % (t, q - 1))
    op.next = q
    return op


class Untranslatable(Exception):
    pass


# ------------------------------------------------------------------ control flow: the interpreter's jump-table stack, followed statically
F = {}  # function name -> number (filled from the enum)


def analyse(scene, funcs, start, jts, is_function):
    """-> {pos: (op, stack)}, [called function numbers] - every instruction the code can reach from `start`,
    with the jump-table stack the interpreter would have there (the same on every path, or Untranslatable)"""
    code, jt = scene.code, scene.jt
    seen, work, calls = {}, [(start, ())], set()

    def J(k):
        if not (0 <= jts + k < len(jt)):
            raise Untranslatable("jump table index %d outside the table" % k)
        return jt[jts + k]

    def go(p, st):
        if not (0 <= p < len(code)):
            raise Untranslatable("jump to %d outside the code" % p)
        work.append((p, st))

    def konst(op, i):
        a = op.args[i]
        if a[0] != "int":
            raise Untranslatable("%s at %d: jump-table operand is not a constant" % (funcs[op.func][0], op.pos))
        return a[1]

    while work:
        p, st = work.pop()
        if p in seen:
            if seen[p][1] != st:
                raise Untranslatable("instruction %d reached with jump-table stacks %s and %s" % (p, seen[p][1], st))
            continue
        op = decode(code, funcs, p)
        seen[p] = (op, st)
        f = op.func
        if f == F["FUNC_END"]:
            continue
        if F["FUNC_IFEQUAL"] <= f <= F["FUNC_IFNOTEQUAL"]:
            k = konst(op, 0)
            go(op.next, st + (k,))
            go(start + J(k), st + (k,))
        elif f == F["FUNC_ELSE"]:
            if not st:
                raise Untranslatable("else with an empty jump-table stack at %d" % p)
            go(start + J(st[-1] + 1), st[:-1])
        elif f in (F["FUNC_ENDIF"], F["FUNC_ENDSWITCH"]):
            if not st:
                raise Untranslatable("endif / endswitch with an empty jump-table stack at %d" % p)
            go(op.next, st[:-1])
        elif F["FUNC_WEQUAL"] <= f <= F["FUNC_WNOTEQUAL"]:
            k = konst(op, 0)
            go(op.next, st + (k,))
            go(start + J(k + 1), st)
        elif f == F["FUNC_LOOP"]:
            if not st:
                raise Untranslatable("loop with an empty jump-table stack at %d" % p)
            go(start + J(st[-1]), st[:-1])
        elif f == F["FUNC_SWITCH"]:
            k = konst(op, 0)
            lo, hi = J(k), J(k + 1)
            if hi - lo > 4096:
                raise Untranslatable("switch over %d values at %d" % (hi - lo + 1, p))
            go(start + J(k + 2), st + (k,))
            for v in range(hi - lo + 1):
                go(start + J(k + 4 + v), st + (k,))
        elif f == F["FUNC_BREAK"]:
            if not st:
                raise Untranslatable("break with an empty jump-table stack at %d" % p)
            go(start + J(st[-1] + 3), st[:-1])
        elif f == F["FUNC_CALLFUNCTION"]:
            if op.args[0][0] == "int":
                n = op.args[0][1]
                if not (0 <= n < len(scene.fcode)):
                    raise Untranslatable("CallFunction %d: no such function" % n)
                calls.add(n)
            else:  # the function number is a variable (PlayerObject's state machine): any function
                calls.add(-1)
            go(op.next, st)
        elif f == F["FUNC_ENDFUNCTION"]:
            if not is_function:
                raise Untranslatable("EndFunction in an object sub at %d" % p)
            if st:
                raise Untranslatable("EndFunction with the jump-table stack not back where it started (%s) at %d" % (st, p))
            continue
        else:
            go(op.next, st)
    return seen, sorted(calls)


# ------------------------------------------------------------------ code generation
def c_bytes(b):
    return '"' + "".join("\\x%02x" % c for c in b) + '"'


# R1X: an instruction whose code (its case body) is at least this many tokens is not copied into every sub that uses
# it: one shared function per instruction (scdop_<name>) runs the same statements - with the same locals, made as the
# copies make them - and gives back opcodeSize for the write-back, which stays in the sub. The native code shrinks
# (fewer cart cache misses), the shared functions stay in the cache.
OUTLINE_TOKENS = 60


class Gen:
    def __init__(self, scene, funcs, fenum, venum, ex, part="all", ng=0, engine_ops=()):
        self.scene, self.funcs, self.fenum, self.venum, self.ex = scene, funcs, fenum, venum, ex
        self.problems = {}
        self.outlined = {}  # instruction name -> the shared function's text
        # REV R4: part "global" (the engine's: the global scripts, function numbers < ng) or "stage" (a scene image's
        # blob: the rest; the global functions and the shared instructions the engine has are called there)
        self.part, self.ng, self.engine_ops = part, ng, set(engine_ops)
        self.extern_ops = set()

    def fcall(self, n):
        """REV R4: the C++ function that runs script function n"""
        if self.part == "stage" and n < self.ng:
            return "scdNativeGlobalF[%d]" % n
        return "scdn_F%d" % n

    def outline(self, op):
        """the shared function's name for this instruction, or None to copy its code into the sub"""
        fname = self.fenum[op.func]
        b = self.body(op)
        if len(b) < OUTLINE_TOKENS:
            return None
        name = "scdop_" + fname
        if self.part == "stage" and fname in self.engine_ops:
            self.extern_ops.add(fname)  # REV R4: the engine's copy (SDRAM if it is hot)
            return name
        if fname not in self.outlined:
            self.outlined[fname] = "\n".join([
                "static int %s(int opcodeSize, byte scriptSub)" % name, "{",
                "    ObjectScript *scriptInfo = &objectScriptList[objectEntityList[objectLoop].type];",
                "    Entity *entity = &objectEntityList[objectLoop];",
                "    Player *player = &playerList[activePlayer];",
                "    SpriteFrame *spriteFrame = nullptr;",
                "    (void)scriptInfo; (void)entity; (void)player; (void)spriteFrame; (void)scriptSub;",
                "    do { %s } while (0);" % cpp.join(b),
                "    return opcodeSize;", "}"])
        return name

    def array_val(self, mode, a, b):
        idx = ("scriptEng.arrayPosition[%d]" % b) if a == 1 else str(b)
        if mode == 0:
            return "objectLoop"
        if mode == 1:
            return idx
        if mode == 2:
            return "%s + objectLoop" % idx
        if mode == 3:
            return "objectLoop - %s" % idx
        return "0"

    def var_name(self, v):
        return self.venum[v] if 0 <= v < len(self.venum) else None

    def var_code(self, kind, i, arg):
        _, mode, a, b, v = arg
        name = self.var_name(v)
        table = self.ex[kind]
        if name is None or name not in table:
            body = table.get("default", ["break", ";"])  # the interpreter's default: nothing
        else:
            body = table[name]
        body = self.known_var(body, v)
        if any(t in cpp.CONTROL_LOCALS for t in body):
            raise Untranslatable("variable %s needs the interpreter's position" % name)
        return "{ const int i = %d; int arrayVal = %s; (void)i; (void)arrayVal; do { %s } while (0); }" % (
            i, self.array_val(mode, a, b), cpp.join(body))

    def known_var(self, body, v):
        """REV R4: cases the interpreter shares between variables (the port's 3D buffers, Script.cpp S5i) find out which
        one from the bytecode - scriptCode[scriptCodePtr - 1]; here it is known: that number, and the variables'
        names as numbers (the enum is Script.cpp's own) - the compiler keeps the one case"""
        pat = ["scriptCode", "[", "scriptCodePtr", "-", "1", "]"]
        if "scriptCodePtr" not in body:
            return body
        out, k = [], 0
        while k < len(body):
            if body[k:k + len(pat)] == pat:
                out.append(str(v))
                k += len(pat)
            else:
                out.append(body[k])
                k += 1
        venum = {n: i for i, n in enumerate(self.venum[:VENUM_FIXED])}
        if v >= VENUM_FIXED or any(t.startswith("VAR_") and t not in venum for t in out):
            return body  # (a variable whose number depends on the build: left to the interpreter)
        return [str(venum[t]) if t in venum else t for t in out]

    def reads(self, op):
        out = []
        for i, arg in enumerate(op.args):
            if arg[0] == "var":
                out.append(self.var_code("read", i, arg))
            elif arg[0] == "int":
                out.append("scriptEng.operands[%d] = %d;" % (i, arg[1]))
            else:  # string: scriptText as the interpreter fills it
                s = arg[1]
                out.append("scriptText[%d] = 0; memcpy(scriptText, %s, %d);" % (len(s), c_bytes(s), len(s)) if s
                           else "scriptText[0] = 0;")
        return out

    def writes(self, op):
        out = []
        for i, arg in enumerate(op.args):
            if arg[0] == "var":
                out.append("if (%d < opcodeSize) %s" % (i, self.var_code("write", i, arg)))
        return out

    def body(self, op):
        fname = self.fenum[op.func]
        b = self.ex["op"].get(fname)
        if b is None:
            b = self.ex["op"].get("default", ["break", ";"])
        if any(t in cpp.CONTROL_LOCALS for t in b):
            raise Untranslatable("instruction %s needs the interpreter's position" % fname)
        return b

    def sub(self, cname, start, jts, is_function):
        """-> (C++ text, called function numbers)"""
        seen, calls = analyse(self.scene, self.funcs, start, jts, is_function)
        J = lambda k: self.scene.jt[jts + k]
        lines = ["static bool %s(byte scriptSub)" % cname, "{",
                 "    int opcodeSize = 0;", "    ObjectScript *scriptInfo = nullptr;", "    Entity *entity = nullptr;",
                 "    Player *player = nullptr;", "    SpriteFrame *spriteFrame = nullptr;",
                 "    (void)opcodeSize; (void)scriptInfo; (void)entity; (void)player; (void)spriteFrame; (void)scriptSub;"]
        order = sorted(seen)
        for n, p in enumerate(order):
            op, st = seen[p]
            fname = self.fenum[op.func]
            nxt = order[n + 1] if n + 1 < len(order) else None
            lines.append("L%d: /* %s */" % (p, self.funcs[op.func][0]))
            code = []
            code += self.reads(op)
            f = op.func
            ctl = True
            if f == F["FUNC_END"]:
                code.append("return true;")
            elif F["FUNC_IFEQUAL"] <= f <= F["FUNC_IFNOTEQUAL"]:
                cond = {"FUNC_IFEQUAL": "!=", "FUNC_IFGREATER": "<=", "FUNC_IFGREATEROREQUAL": "<", "FUNC_IFLOWER": ">=",
                        "FUNC_IFLOWEROREQUAL": ">", "FUNC_IFNOTEQUAL": "=="}[fname]
                k = op.args[0][1]
                code.append("if (scriptEng.operands[1] %s scriptEng.operands[2]) goto L%d;" % (cond, start + J(k)))
                if op.next != nxt:
                    code.append("goto L%d;" % op.next)
            elif F["FUNC_WEQUAL"] <= f <= F["FUNC_WNOTEQUAL"]:
                cond = {"FUNC_WEQUAL": "!=", "FUNC_WGREATER": "<=", "FUNC_WGREATEROREQUAL": "<", "FUNC_WLOWER": ">=",
                        "FUNC_WLOWEROREQUAL": ">", "FUNC_WNOTEQUAL": "=="}[fname]
                k = op.args[0][1]
                code.append("if (scriptEng.operands[1] %s scriptEng.operands[2]) goto L%d;" % (cond, start + J(k + 1)))
                if op.next != nxt:
                    code.append("goto L%d;" % op.next)
            elif f == F["FUNC_ELSE"]:
                code.append("goto L%d;" % (start + J(st[-1] + 1)))
            elif f in (F["FUNC_ENDIF"], F["FUNC_ENDSWITCH"]):
                if op.next != nxt:
                    code.append("goto L%d;" % op.next)
            elif f == F["FUNC_LOOP"]:
                code.append("goto L%d;" % (start + J(st[-1])))
            elif f == F["FUNC_SWITCH"]:
                k = op.args[0][1]
                lo, hi = J(k), J(k + 1)
                cases = " ".join("case %d: goto L%d;" % (lo + v, start + J(k + 4 + v)) for v in range(hi - lo + 1))
                code.append("switch (scriptEng.operands[1]) { %s default: goto L%d; }" % (cases, start + J(k + 2)))
            elif f == F["FUNC_BREAK"]:
                code.append("goto L%d;" % (start + J(st[-1] + 3)))
            elif f == F["FUNC_CALLFUNCTION"]:
                if op.args[0][0] == "int":
                    code.append("if (%s(scriptSub)) return true;" % self.fcall(op.args[0][1]))
                else:
                    code.append("if (scdn_call(scriptSub)) return true;")
                if op.next != nxt:
                    code.append("goto L%d;" % op.next)
            elif f == F["FUNC_ENDFUNCTION"]:
                code.append("return false;")
            else:
                ctl = False
            oname = self.outline(op) if not ctl else None
            if oname:
                code.append("opcodeSize = %s(%d, scriptSub);" % (oname, self.funcs[f][1]))
                w = self.writes(op)
                if w:
                    code.append("if (opcodeSize > 0) { %s }" % " ".join(w))
                if op.next != nxt:
                    code.append("goto L%d;" % op.next)
            elif not ctl:
                code.append("scriptInfo = &objectScriptList[objectEntityList[objectLoop].type];")
                code.append("entity = &objectEntityList[objectLoop];")
                code.append("player = &playerList[activePlayer];")
                code.append("spriteFrame = nullptr;")
                code.append("opcodeSize = %d;" % self.funcs[f][1])
                code.append("do { %s } while (0);" % cpp.join(self.body(op)))
                w = self.writes(op)
                if w:
                    code.append("if (opcodeSize > 0) { %s }" % " ".join(w))
                if op.next != nxt:
                    code.append("goto L%d;" % op.next)
            lines.append("    { " + " ".join(code) + " }")
        lines.append("    return false;")  # not reached: every path ends in End / EndFunction
        lines.append("}")
        return "\n".join(lines), calls, len(order)


def code_hash(scene, part="all", g=0, gj=0, ng=0):
    """the same hash the engine computes at a stage load (scd_native_bind): FNV-1a over the 32-bit words.
    REV R4: part "global" - the global scripts' code, jump table and functions (the first g, gj, ng); "stage" - the rest
    and the whole scene's counts"""
    h = 0x811C9DC5

    def mix(v):
        nonlocal h
        h = ((h ^ (v & 0xFFFFFFFF)) * 0x01000193) & 0xFFFFFFFF

    if part == "all":
        for v in scene.code:
            mix(v)
        for v in scene.jt:
            mix(v)
        for c, j in zip(scene.fcode, scene.fjt):
            mix(c)
            mix(j)
        return h
    lo_c, hi_c = (0, g) if part == "global" else (g, len(scene.code))
    lo_j, hi_j = (0, gj) if part == "global" else (gj, len(scene.jt))
    lo_f, hi_f = (0, ng) if part == "global" else (ng, len(scene.fcode))
    for v in scene.code[lo_c:hi_c]:
        mix(v)
    for v in scene.jt[lo_j:hi_j]:
        mix(v)
    for c, j in zip(scene.fcode[lo_f:hi_f], scene.fjt[lo_f:hi_f]):
        mix(c)
        mix(j)
    for v in ((g, gj, ng) if part == "global" else (len(scene.code), len(scene.jt), len(scene.fcode))):
        mix(v)
    return h


def global_part(data):
    """REV R4: (code words, jump table words, functions) of the global scripts (GS000) - the same in every stage that
    loads them (they load first: tools check it, the engine hashes it)"""
    c, j, cp, jp, fc, fj = load_bytecode(data.read("Data/Scripts/ByteCode/GS000.bin"))
    return len(c), len(j), len(fc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--ii", required=True, help="engine/Script.cpp after the preprocessor, with the build's flags")
    ap.add_argument("--script-cpp", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "engine", "Script.cpp"))
    ap.add_argument("--scene", default="1:0")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="", help="TYPE:SUB,... (type names as the game names them, sub = main/player/draw/setup)")
    ap.add_argument("--hot", default="", help="R1V: TYPE:SUB,... and F<n>,... whose code goes into SDRAM on the console "
                    "(SCD_HOT: the ones that run every frame)")
    ap.add_argument("--part", default="all", choices=("all", "global", "stage"),
                    help="REV R4: global = the global scripts only (into the engine: engine/ScriptNative.inc), stage = "
                    "the scene's own (a blob for its image: tools/build_game_data.py --native), all = everything (R1-R3)")
    ap.add_argument("--ops", default="", help="REV R4: the engine's list (global: written; stage: read) - "
                    "the global part's sizes, its native functions and shared instructions")
    ap.add_argument("--engine-sum", default="0", help="REV R4 (stage): the engine image's checksum the blob is for")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    funcs, fenum, venum = engine_tables(a.script_cpp)
    for n, name in enumerate(fenum):
        F[name] = n
    ex = cpp.extract(a.ii)
    data = sr.Data(a.data)
    lst, pos = (int(x) for x in a.scene.split(":"))
    scene = Scene(data, lst, pos)
    part = a.part
    g = gj = ng = 0
    engine_ops, engine_fns = set(), set()
    gsz = global_part(data)
    gc_ok = scene.code[:gsz[0]] == load_bytecode(data.read("Data/Scripts/ByteCode/GS000.bin"))[0] and len(scene.fcode) >= gsz[2]
    if part == "global":
        if not gc_ok:
            sys.exit("--part global: scene %s does not start with the global scripts" % a.scene)
        g, gj, ng = gsz
    elif part == "stage":
        if not a.ops or not os.path.exists(a.ops):
            sys.exit("--part stage needs --ops (engine/ScriptNative.ops, written by --part global)")
        for line in open(a.ops):
            w = line.split()
            if w and w[0] == "global":
                eg, egj, eng = int(w[1]), int(w[2]), int(w[3])
            elif w and w[0] == "op":
                engine_ops.add(w[1])
            elif w and w[0] == "fn":
                engine_fns.add(int(w[1]))
        if gc_ok and (eg, egj, eng) == gsz:
            g, gj, ng = gsz  # the scene runs the global scripts: the engine has those
        else:
            engine_ops, engine_fns = set(), set()  # a scene of its own (special stages, menus): all of it here
    gen = Gen(scene, funcs, fenum, venum, ex, part, ng, engine_ops)
    only = set()
    for item in filter(None, a.only.split(",")):
        t, s_ = item.rsplit(":", 1)
        only.add((t.strip().replace(" ", ""), s_.strip()))
    hot_subs, hot_funcs, hot_ops = set(), set(), set()
    for item in filter(None, (x.strip() for x in a.hot.split(","))):
        if re.fullmatch(r"F\d+", item):
            hot_funcs.add(int(item[1:]))
        elif item.startswith("op:"):  # R1X: a shared instruction function, e.g. op:FUNC_DRAWSPRITESCREENXY
            hot_ops.add(item)
        else:
            t, s_ = item.rsplit(":", 1)
            hot_subs.add((t.strip().replace(" ", ""), s_.strip()))
    if part == "stage":
        hot_subs, hot_funcs, hot_ops = set(), set(), set()  # REV R4: no SDRAM for a blob

    out_subs, out_funcs, done_funcs, skipped = [], {}, {}, []
    count_ops = 0
    nf = len(scene.fcode)

    def in_part(n):
        """function n is this part's to translate"""
        if part == "global":
            return n < ng
        if part == "stage":
            return n >= ng
        return True

    def real(m):
        return 0 <= scene.fcode[m] < len(scene.code)  # not an unused slot ("no script")

    def want_function(n):
        """translate function n and everything it calls; False if any of it cannot be (-1: all functions)"""
        if n == -1:
            ok = True
            for m in range(nf):
                if real(m):
                    ok = want_function(m) and ok
            done_funcs[-1] = ok
            return ok
        if n in done_funcs:
            return done_funcs[n]
        if part == "stage" and n < ng:  # the engine's: native there, or not
            done_funcs[n] = n in engine_fns
            return done_funcs[n]
        if part == "global" and n >= ng:  # a stage's function: the engine calls the blob's (scd_native_stage_fn)
            done_funcs[n] = True
            return True
        done_funcs[n] = True  # recursion: assume yes, corrected below
        try:
            text, calls, nops = gen.sub("scdn_F%d" % n, scene.fcode[n], scene.fjt[n], True)
        except (Untranslatable, ValueError) as e:
            done_funcs[n] = False
            skipped.append(("function %d" % n, str(e)))
            return False
        for c in calls:
            if not want_function(c):
                done_funcs[n] = False
                skipped.append(("function %d" % n, "calls function %d (not translatable)" % c))
                return False
        out_funcs[n] = (text, nops, -1 in calls)
        return True

    for t in sorted(scene.subs):
        name = scene.names[t] if t < len(scene.names) else "type%d" % t
        for s_, (cp, jp) in enumerate(scene.subs[t]):
            if cp >= len(scene.code) or cp < 0:
                continue  # no such sub (the "no script" index)
            if part == "global" and cp >= g or part == "stage" and cp < g:
                continue  # REV R4: the other part's
            if only and (name.replace(" ", ""), SUBS[s_]) not in only:
                continue
            cname = "scdn_S%d" % cp
            if any(x[1] == cname for x in out_subs):
                continue  # two types sharing one sub
            try:
                text, calls, nops = gen.sub(cname, cp, jp, False)
            except (Untranslatable, ValueError) as e:
                skipped.append(("%s %s" % (name, SUBS[s_]), str(e)))
                continue
            if not all(want_function(c) for c in calls):
                skipped.append(("%s %s" % (name, SUBS[s_]), "calls a function that cannot be translated"))
                continue
            out_subs.append((cp, cname, jp, text, name, SUBS[s_], nops, -1 in calls))
            count_ops += nops
    if part == "stage":
        # REV R4: every function of the scene, called by a sub or not - a variable call (the global scripts' too:
        # CallFunction(object.state) ...) may reach any of them, and is native only when they all are (allFuncs)
        for n in range(ng, nf):
            if real(n):
                want_function(n)
    # functions that ended up not translatable after a sub took them: drop the subs that call them
    bad = {n for n, ok in done_funcs.items() if not ok}
    if bad:
        keep = []
        for item in out_subs:
            if any(("scdn_F%d(" % n) in item[3] or ("scdNativeGlobalF[%d]" % n) in item[3] for n in bad if n >= 0) or \
                    (-1 in bad and "scdn_call(" in item[3]):
                skipped.append(("%s %s" % (item[4], item[5]), "calls a function that cannot be translated"))
            else:
                keep.append(item)
        out_subs = keep
        for n in [n for n in out_funcs if any(("scdn_F%d(" % b) in out_funcs[n][0] for b in bad if b >= 0)]:
            del out_funcs[n]
    h = code_hash(scene, part, g, gj, ng)
    var_call = done_funcs.get(-1) is True
    stage_funcs_all = all(n in out_funcs for n in range(ng, nf) if real(n)) if part == "stage" else True
    with open(a.out, "w") as f:
        w = f.write
        what = {"all": "", "global": " - the global scripts (REV R4: the engine's part)",
                "stage": " - the scene's own scripts (REV R4: its image's blob)"}[part]
        w("// PORT32X R1U: native scripts for scene %d:%d (%s)%s - generated by tools/scd_aot.py from the game's data.\n"
          "// Do not edit, do not publish (made from the game's bytecode). %d object subs, %d functions.\n" % (
              lst, pos, scene.label, what, len(out_subs), len(out_funcs)))
        if part == "stage":
            w('#include "RetroEngine.hpp"\n')
            w("extern \"C\" bool (*const scdNativeGlobalF[])(byte); // the engine's global functions (ScriptNative.inc)\n")
            for name in sorted(gen.extern_ops):
                w("extern \"C\" int scdop_%s(int opcodeSize, byte scriptSub); // the engine's\n" % name)
            w("extern \"C\" bool scd_native_stage_fn(int n, byte scriptSub);\n")
        if part == "global":  # REV R4: the shared instructions with C names - the blobs call them
            for name in sorted(gen.outlined):
                w("extern \"C\" int scdop_%s(int opcodeSize, byte scriptSub)%s;\n" % (
                    name, " SCD_HOT" if ("op:" + name) in hot_ops else ""))
                w(gen.outlined[name].replace("static int scdop_%s(" % name, "extern \"C\" int scdop_%s(" % name) + "\n")
            w("extern \"C\" bool scd_native_stage_fn(int n, byte scriptSub); // Script.cpp: a stage's function (its blob)\n")
        w("namespace {\n")
        for n in sorted(out_funcs):
            w("static bool scdn_F%d(byte scriptSub)%s;\n" % (n, " SCD_HOT" if n in hot_funcs else ""))
        for cp, cname, jp, text, name, sub, nops, vc in out_subs:  # R1V: the ones that run every frame into SDRAM
            if (name.replace(" ", ""), sub) in hot_subs:
                w("static bool %s(byte scriptSub) SCD_HOT; // %s, %s\n" % (cname, name, sub))
        if var_call:
            # CallFunction with a variable: the interpreter takes scriptFunctionList[n] - every function is native
            w("static bool scdn_call(byte scriptSub)\n{\n    switch (scriptEng.operands[0]) {\n")
            for n in range(nf):
                if not real(n):
                    continue
                if part == "global" and n >= ng:
                    continue
                if n in out_funcs or part == "stage" and n < ng:
                    w("        case %d: return %s(scriptSub);\n" % (n, gen.fcall(n)))
            if part == "global":
                w("        default: return scd_native_stage_fn(scriptEng.operands[0], scriptSub); /* a stage's (its blob) */\n    }\n}\n")
            else:
                w("        default: return true; /* no such function: the interpreter would run garbage - stop */\n    }\n}\n")
        if part != "global":
            for name in sorted(gen.outlined):  # R1X: the shared instruction functions
                w("\n// instruction %s (shared)\n" % name + gen.outlined[name].replace(
                    "static int scdop_%s(int opcodeSize, byte scriptSub)" % name,
                    "static int scdop_%s(int opcodeSize, byte scriptSub)%s;\nstatic int scdop_%s(int opcodeSize, byte scriptSub)" % (
                        name, " SCD_HOT" if ("op:" + name) in hot_ops else "", name)) + "\n")
        for n in sorted(out_funcs):
            w("\n" + out_funcs[n][0] + "\n")
        for cp, cname, jp, text, name, sub, nops, vc in out_subs:
            w("\n// %s, %s: %d instructions\n" % (name, sub, nops) + text + "\n")
        subs_name = "scdNativeSubs" if part != "stage" else "blobSubs"
        w("const ScdNativeSub %s[] = {\n" % subs_name)
        for cp, cname, jp, text, name, sub, nops, vc in sorted(out_subs):
            w("    { %d, %d, %s, %d },\n" % (cp, jp, cname, 1 if vc else 0))
        w("    { 0x7FFFFFFF, 0, nullptr, 0 }\n};\n")
        if part != "stage":
            w("const int scdNativeSubCount = %d;\n" % len(out_subs))
            w("const unsigned int scdNativeHash = 0x%08Xu;\n" % h)
            if part == "global":
                w("const int scdNativeCodeCount = %d, scdNativeJumpCount = %d, scdNativeFuncCount = %d;\n" % (g, gj, ng))
            else:
                w("const int scdNativeCodeCount = %d, scdNativeJumpCount = %d, scdNativeFuncCount = %d;\n" % (
                    len(scene.code), len(scene.jt), len(scene.fcode)))
            w("} // namespace\n")
            if part == "global":
                w("extern \"C\" bool (*const scdNativeGlobalF[%d])(byte) = {\n" % max(1, ng))
                for n in range(ng):
                    w("    %s,\n" % (("scdn_F%d" % n) if n in out_funcs else "nullptr"))
                w("};\n")
                # REV R4: the shared instructions the blobs call - listed, so the engine's link keeps them all
                w("extern \"C\" int (*const scdNativeOps[])(int, byte) = {\n")
                for name in sorted(gen.outlined):
                    w("    scdop_%s,\n" % name)
                w("    nullptr\n};\n")
        else:
            w("bool (*const blobFuncs[%d])(byte) = {\n" % max(1, nf))
            for n in range(nf):
                w("    %s,\n" % (("scdn_F%d" % n) if n >= ng and n in out_funcs else "nullptr"))
            w("};\n} // namespace\n")
            w("extern \"C\" const ScdNativeBlob scdNativeBlob __attribute__((section(\".blob_header\"), used)) = {\n")
            w("    0x5343444Eu, 1, %su, 0x%08Xu, %d, %d, %d, %d, %d, %d, %d, blobSubs, blobFuncs, %d\n};\n" % (
                a.engine_sum, h, g, gj, ng, len(scene.code), len(scene.jt), nf, len(out_subs), 1 if stage_funcs_all else 0))
    if part == "global" and a.ops:
        with open(a.ops, "w") as f:
            f.write("global %d %d %d\n" % (g, gj, ng))
            for name in sorted(gen.outlined):
                f.write("op %s\n" % name)
            for n in sorted(out_funcs):
                f.write("fn %d\n" % n)
            f.write("hash %08X\n" % h)
    print("%s: scene %s%s, %d object subs + %d functions native (%d instructions), %d left to the interpreter, hash %08X" % (
        a.out, scene.label, "" if part == "all" else " (%s part)" % part, len(out_subs), len(out_funcs),
        count_ops + sum(v[1] for v in out_funcs.values()), len(skipped), h))
    if a.report:
        for what, why in skipped:
            print("  interpreted: %-40s %s" % (what, why))


if __name__ == "__main__":
    main()
