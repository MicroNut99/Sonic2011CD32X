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
    assert len(fenum) >= len(funcs), (len(fenum), len(funcs))
    return funcs, fenum, venum


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


class Gen:
    def __init__(self, scene, funcs, fenum, venum, ex):
        self.scene, self.funcs, self.fenum, self.venum, self.ex = scene, funcs, fenum, venum, ex
        self.problems = {}

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
        if any(t in cpp.CONTROL_LOCALS for t in body):
            raise Untranslatable("variable %s needs the interpreter's position" % name)
        return "{ const int i = %d; int arrayVal = %s; (void)i; (void)arrayVal; do { %s } while (0); }" % (
            i, self.array_val(mode, a, b), cpp.join(body))

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
                    code.append("if (scdn_F%d(scriptSub)) return true;" % op.args[0][1])
                else:
                    code.append("if (scdn_call(scriptSub)) return true;")
                if op.next != nxt:
                    code.append("goto L%d;" % op.next)
            elif f == F["FUNC_ENDFUNCTION"]:
                code.append("return false;")
            else:
                ctl = False
            if not ctl:
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


def code_hash(scene):
    """the same hash the engine computes at a stage load (scd_native_hash): FNV-1a over the 32-bit words"""
    h = 0x811C9DC5

    def mix(v):
        nonlocal h
        h = ((h ^ (v & 0xFFFFFFFF)) * 0x01000193) & 0xFFFFFFFF

    for v in scene.code:
        mix(v)
    for v in scene.jt:
        mix(v)
    for c, j in zip(scene.fcode, scene.fjt):
        mix(c)
        mix(j)
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--ii", required=True, help="engine/Script.cpp after the preprocessor, with the build's flags")
    ap.add_argument("--script-cpp", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "engine", "Script.cpp"))
    ap.add_argument("--scene", default="1:0")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="", help="TYPE:SUB,... (type names as the game names them, sub = main/player/draw/setup)")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    funcs, fenum, venum = engine_tables(a.script_cpp)
    for n, name in enumerate(fenum):
        F[name] = n
    ex = cpp.extract(a.ii)
    data = sr.Data(a.data)
    lst, pos = (int(x) for x in a.scene.split(":"))
    scene = Scene(data, lst, pos)
    gen = Gen(scene, funcs, fenum, venum, ex)
    only = set()
    for item in filter(None, a.only.split(",")):
        t, s = item.rsplit(":", 1)
        only.add((t.strip().replace(" ", ""), s.strip()))

    out_subs, out_funcs, done_funcs, skipped = [], {}, {}, []
    count_ops = 0

    def want_function(n):
        """translate function n and everything it calls; False if any of it cannot be (-1: all functions)"""
        if n == -1:
            ok = True
            for m in range(len(scene.fcode)):
                if scene.fcode[m] < 0 or scene.fcode[m] >= len(scene.code):
                    continue  # an unused slot ("no script")
                ok = want_function(m) and ok
            done_funcs[-1] = ok
            return ok
        if n in done_funcs:
            return done_funcs[n]
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
        out_funcs[n] = (text, nops)
        return True

    for t in sorted(scene.subs):
        name = scene.names[t] if t < len(scene.names) else "type%d" % t
        for s, (cp, jp) in enumerate(scene.subs[t]):
            if cp >= len(scene.code) or cp < 0:
                continue  # no such sub (the "no script" index)
            if only and (name.replace(" ", ""), SUBS[s]) not in only:
                continue
            cname = "scdn_S%d" % cp
            if any(x[1] == cname for x in out_subs):
                continue  # two types sharing one sub
            try:
                text, calls, nops = gen.sub(cname, cp, jp, False)
            except (Untranslatable, ValueError) as e:
                skipped.append(("%s %s" % (name, SUBS[s]), str(e)))
                continue
            if not all(want_function(c) for c in calls):
                skipped.append(("%s %s" % (name, SUBS[s]), "calls a function that cannot be translated"))
                continue
            out_subs.append((cp, cname, jp, text, name, SUBS[s], nops))
            count_ops += nops
    # functions that ended up not translatable after a sub took them: drop the subs that call them
    bad = {n for n, ok in done_funcs.items() if not ok}
    if bad:
        keep = []
        for item in out_subs:
            if any(("scdn_F%d(" % n) in item[3] for n in bad if n >= 0) or (-1 in bad and "scdn_call(" in item[3]):
                skipped.append(("%s %s" % (item[4], item[5]), "calls a function that cannot be translated"))
            else:
                keep.append(item)
        out_subs = keep
    h = code_hash(scene)
    with open(a.out, "w") as f:
        w = f.write
        w("// PORT32X R1U: native scripts for scene %d:%d (%s) - generated by tools/scd_aot.py from the game's data.\n"
          "// Do not edit, do not publish (made from the game's bytecode). %d object subs, %d functions.\n" % (
              lst, pos, scene.label, len(out_subs), len(out_funcs)))
        w("namespace {\n")
        for n in sorted(out_funcs):
            w("static bool scdn_F%d(byte scriptSub);\n" % n)
        if done_funcs.get(-1):
            # CallFunction with a variable: the interpreter takes scriptFunctionList[n] - every function is native
            w("static bool scdn_call(byte scriptSub)\n{\n    switch (scriptEng.operands[0]) {\n")
            for n in sorted(out_funcs):
                w("        case %d: return scdn_F%d(scriptSub);\n" % (n, n))
            w("        default: return true; /* no such function: the interpreter would run garbage - stop */\n    }\n}\n")
        for n in sorted(out_funcs):
            w("\n" + out_funcs[n][0] + "\n")
        for cp, cname, jp, text, name, sub, nops in out_subs:
            w("\n// %s, %s: %d instructions\n" % (name, sub, nops) + text + "\n")
        w("struct ScdNativeSub {\n    int codeStart, jumpTableStart;\n    bool (*fn)(byte);\n};\n")
        w("const ScdNativeSub scdNativeSubs[] = {\n")
        for cp, cname, jp, text, name, sub, nops in sorted(out_subs):
            w("    { %d, %d, %s },\n" % (cp, jp, cname))
        w("    { 0x7FFFFFFF, 0, nullptr }\n};\n")
        w("const int scdNativeSubCount = %d;\n" % len(out_subs))
        w("const unsigned int scdNativeHash = 0x%08Xu;\n" % h)
        w("const int scdNativeCodeCount = %d, scdNativeJumpCount = %d, scdNativeFuncCount = %d;\n" % (
            len(scene.code), len(scene.jt), len(scene.fcode)))
        w("} // namespace\n")
    print("%s: scene %s, %d object subs + %d functions native (%d instructions), %d left to the interpreter, hash %08X" % (
        a.out, scene.label, len(out_subs), len(out_funcs), count_ops + sum(v[1] for v in out_funcs.values()), len(skipped), h))
    if a.report:
        for what, why in skipped:
            print("  interpreted: %-40s %s" % (what, why))


if __name__ == "__main__":
    main()
