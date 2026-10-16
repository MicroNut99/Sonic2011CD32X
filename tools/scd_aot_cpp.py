#!/usr/bin/env python3
"""PORT32X R1U: the interpreter's own code, per script instruction and per variable.

Reads engine/Script.cpp after the C preprocessor (the compiler's own view: `g++ -E -P` with the build's
flags) and cuts ProcessScript into the pieces the native script translator (tools/scd_aot.py) puts
together for one instruction:
  read[var]    - the code that loads variable `var` into scriptEng.operands[i] (uses i, arrayVal)
  write[var]   - the code that stores scriptEng.operands[i] back into variable `var`
  op[func]     - the code of instruction `func` (its `case FUNC_...:` body, up to and including the
                 break that leaves the switch - wrapped by the translator in do { } while (0))
Nothing is rewritten: the translator emits exactly these statements, in the interpreter's order,
so a translated instruction does what the interpreter does, statement for statement.
"""
import re
import sys

TOKEN = re.compile(
    r'\s+|'
    r'(?P<str>"(?:\\.|[^"\\])*")|'
    r"(?P<chr>'(?:\\.|[^'\\])*')|"
    r'(?P<num>(?:0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][+-]?\d+)?)[uUlLfF]*)|'
    r'(?P<id>[A-Za-z_]\w*)|'
    r'(?P<op>::|->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|[{}()\[\];:,.?<>=!~+\-*/%&|^#])')


def tokenize(src):
    out, pos = [], 0
    while pos < len(src):
        m = TOKEN.match(src, pos)
        if not m:
            raise ValueError("cannot tokenize at: %r" % src[pos:pos + 40])
        pos = m.end()
        if m.lastgroup:
            out.append(m.group(m.lastgroup))
    return out


def match_close(toks, i):
    """toks[i] is '(' '[' or '{' -> index of its partner"""
    o = toks[i]
    c = {'(': ')', '[': ']', '{': '}'}[o]
    d = 0
    for j in range(i, len(toks)):
        if toks[j] == o:
            d += 1
        elif toks[j] == c:
            d -= 1
            if d == 0:
                return j
    raise ValueError("unbalanced " + o)


def find_seq(toks, seq, start=0, end=None):
    end = len(toks) if end is None else end
    n = len(seq)
    for i in range(start, end - n + 1):
        if toks[i:i + n] == seq:
            return i
    return -1


def function_body(toks, name_seq):
    """the {...} body of the function whose header starts with name_seq (a definition, not a declaration)"""
    i = 0
    while True:
        i = find_seq(toks, name_seq, i)
        if i < 0:
            raise ValueError("function not found: " + " ".join(name_seq))
        p = toks.index('(', i)
        q = match_close(toks, p)
        if toks[q + 1] == '{':
            return q + 1, match_close(toks, q + 1)
        i = q


def switch_cases(toks, sw):
    """toks[sw] == 'switch': -> list of (labels, body tokens) in source order; a body runs from its labels to
    the next label at the switch's own depth (fall-through is the translator's business)"""
    p = toks.index('(', sw)
    q = match_close(toks, p)
    assert toks[q + 1] == '{', "switch without braces"
    b0, b1 = q + 1, match_close(toks, q + 1)
    cases, labels, body = [], [], []
    i, depth = b0 + 1, 0
    while i < b1:
        t = toks[i]
        if depth == 0 and t in ('case', 'default'):
            if body or not labels:
                if labels:
                    cases.append((labels, body))
                labels, body = [], []
            # label: case <expr> :   (a '::' inside the expression is one token, so the first ':' ends it)
            j = i + 1
            while toks[j] != ':':
                j += 1
            labels.append('default' if t == 'default' else ' '.join(toks[i + 1:j]))
            i = j + 1
            continue
        if t in '{([':
            k = match_close(toks, i)
            body.extend(toks[i:k + 1])
            i = k + 1
            continue
        body.append(t)
        i += 1
    if labels:
        cases.append((labels, body))
    return cases, (b0, b1)


def ends_with_break(body):
    """the last statement of a case leaves the switch (break / return) - otherwise it falls through"""
    t = [x for x in body]
    while t and t[-1] == '}':
        t.pop()
    return len(t) >= 2 and t[-1] == ';' and (t[-2] == 'break' or 'return' in t[-6:])


def join(toks):
    """tokens -> C++ text (a space between tokens: always valid after the preprocessor)"""
    return ' '.join(toks)


def extract(ii_path):
    src = open(ii_path, errors='replace').read()
    toks = tokenize(src)
    b0, b1 = function_body(toks, ['void', 'ProcessScript', '('])
    seq = ['switch', '(', 'scriptCode', '[', 'scriptCodePtr', '++', ']', ')']
    found, i = [], b0
    while True:
        i = find_seq(toks, seq, i, b1)
        if i < 0:
            break
        found.append(i)
        i += 1
    # in order: read array mode, READ variables, write array mode, WRITE variables
    assert len(found) == 4, "expected 4 operand switches in ProcessScript, found %d" % len(found)
    opsw = find_seq(toks, ['switch', '(', 'opcode', ')'], b0, b1)
    assert opsw > 0, "no switch (opcode)"
    res = {}
    for name, sw in (('read', found[1]), ('write', found[3]), ('op', opsw)):
        cases, _ = switch_cases(toks, sw)
        table = {}
        for idx, (labels, body) in enumerate(cases):
            full = list(body)
            k = idx
            while not ends_with_break(full) and k + 1 < len(cases):  # fall through into the next case
                k += 1
                full += cases[k][1]
            for lab in labels:
                table[lab] = full
        res[name] = table
    # the locals ProcessScript gives an instruction's code (the translator declares the same)
    res['op_prologue'] = None
    return res


CONTROL_LOCALS = ('scriptCodePtr', 'scriptCodeStart', 'jumpTableStart', 'running', 'jumpTableStack',
                  'jumpTableStackPos', 'functionStack', 'functionStackPos', 'scriptCodeOffset')


def uses(body, names):
    return sorted(set(t for t in body if t in names))


def main():
    ex = extract(sys.argv[1])
    print("read cases %d, write cases %d, instruction cases %d" % (len(ex['read']), len(ex['write']), len(ex['op'])))
    for lab, body in sorted(ex['op'].items()):
        u = uses(body, CONTROL_LOCALS + ('continue', 'return', 'goto'))
        if u:
            print("  %-28s uses %s" % (lab, ", ".join(u)))
    for name in ('read', 'write'):
        for lab, body in sorted(ex[name].items()):
            u = uses(body, CONTROL_LOCALS + ('continue', 'return', 'goto', 'opcodeSize'))
            if u:
                print("  %s %-28s uses %s" % (name, lab, ", ".join(u)))


if __name__ == '__main__':
    main()
