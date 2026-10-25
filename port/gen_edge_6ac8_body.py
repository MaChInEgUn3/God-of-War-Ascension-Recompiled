"""gen_edge_6ac8_body.py <spu_recomp.c> <out.h>: the 6AC8 loop macros for port/gow_edge_native_6ac8.c.

The native 6AC8 stage replaces the loop nest from loc_00006BE0 (outer loop, entered only by falling through
from 6AC8's entry block; its back edge jumps to the label) to loc_00006F98 (the exit, also reached by the
skip in the entry block when the count is not positive and by the 6FC0 path) of the d935 EDGE job module's
function 6AC8. v1 runs the lifted statements in C locals, and the packed versions run the last outer
iteration with them. Those statements are game code, so the repo does not carry them: this script takes
them from the d935 module's lift (jobmod_d93503647dd8_e6d98/spu_recomp.c; the copy in function 6AC8 itself:
the alternate-entry functions 6BE0, 6DE0, 6F40 and 6F98 keep their lifted copies) and writes
gow_edge_6ac8_body.h into the build tree:

  GOW_6A_REGS()   u128 locals for every register the loop nest mentions
  GOW_6A_LOAD()   rN = g[N] for each of them
  GOW_6A_STORE()  g[N] = rN for every register it writes
  GOW_6A_A()      loc_00006BE0 up to `if (r85 == 0) goto loc_00006F40`
  GOW_6A_PRE()    the statements after that test, up to loc_00006DE0
  GOW_6A_INNER()  loc_00006DE0 up to its back edge `if (r18 == 0) goto loc_00006DE0`
  GOW_6A_TAIL()   loc_00006F40 up to the outer back edge `if (r76 != 0) goto loc_00006BE0`

The control flow (do { A; if (r85 != 0) { PRE; do INNER while (r18 == 0); } TAIL } while (r76 != 0)) is
written out in gow_edge_native_6ac8.c. This script checks that the lift has exactly that shape (no other
label, branch, call or return inside; no goto from outside into 6BE0, 6DE0 or 6F40; the outer back edge
followed by loc_00006F98), that loads happen only in A and INNER and stores only in TAIL, that the
registers the nest reads before writing are exactly the loop invariants plus r67/r68, and it evaluates the
blocks symbolically: A's gathers and transposes, the first two spheres' squared distances and distance,
the eight accumulators' initial value, PRE's copies, INNER's whole carried state (weight of sphere j into
the accumulators, distance of sphere j+1, squared distance of sphere j+2, the shifted sphere registers)
and TAIL's eight stores (address, value, order) must be exactly the dataflow the packed path was written
for. It fails otherwise. Statements: ctx->gpr[N] -> rN, spu_ls_read128/write128(ctx, ...) -> LD/ST(...).
The port's CMakeLists.txt runs it whenever the lift changes. A lab lift works as well (a probe wrapper
renames a function to _impl, --blocks adds counters at labels).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, newline="").read().replace("\r\n", "\n").split("\n")
GPR = re.compile(r"ctx->gpr\[(\d+)\]")
ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (.*);$")
T_A = "if (ctx->gpr[85]._u32[0] == 0) goto loc_00006F40;"
T_INNER = "if (ctx->gpr[18]._u32[0] == 0) goto loc_00006DE0;"
T_OUTER = "if (ctx->gpr[76]._u32[0] != 0) goto loc_00006BE0;"


def func(addr):
    hits = [i for i, l in enumerate(lines)
            if re.match(rf"^void \w+_spu_func_{addr}(?:_impl)?\(spu_context\* ctx\) \{{", l)]
    # a probe lift has both the _impl and a wrapper with the plain name: take the one with labels in it
    for fn in hits:
        end = next(i for i in range(fn + 1, len(lines)) if lines[i].startswith("}"))
        if any(re.match(r"^\s*loc_", l) for l in lines[fn:end]):
            return fn, end
    sys.exit(f"function {addr} not found")


fn, end = func("00006AC8")
seq = []
for l in lines[fn + 1:end]:
    s = re.sub(r"g_fnp_blk\[\d+\]\+\+;", "", l.strip()).strip()   # --blocks probe counters
    m = re.match(r"^loc_([0-9A-F]{8}):\s*(.*)$", s)
    if m:
        seq.append(("label", m.group(1)))
        s = m.group(2).strip()
    if not s or s in ("/* branch hint (ignored) */;", "/* nop */;"):
        continue
    seq.append(("stmt", s))
for k, s in seq:   # nothing outside the nest may jump into it
    for t in re.findall(r"goto loc_([0-9A-F]{8})", s):
        if t in ("00006BE0", "00006DE0", "00006F40") and s not in (T_A, T_INNER, T_OUTER):
            sys.exit(f"6AC8: a jump into the loop nest from outside: {s}")
for t, back in (("00006BE0", T_OUTER), ("00006DE0", T_INNER), ("00006F40", T_A)):
    n = sum(1 for k, s in seq if k == "stmt" and f"goto loc_{t};" in s)
    if n != 1:
        sys.exit(f"6AC8: {n} jumps to loc_{t} (want 1: {back})")


def straight(pos, stop, what):
    """statements from seq[pos] up to the statement `stop` (exclusive); no other control flow"""
    out = []
    while pos < len(seq) and seq[pos] != ("stmt", stop):
        k, s = seq[pos]
        if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or "SPU_RET" in s or "pc =" in s \
                or s.startswith("if") or s.startswith("{"):
            sys.exit(f"6AC8 {what}: unexpected control flow: {k} {s}")
        out.append(s); pos += 1
    if pos == len(seq):
        sys.exit(f"6AC8 {what}: `{stop}` not found")
    return out, pos + 1


try:
    p = seq.index(("label", "00006BE0")) + 1
except ValueError:
    sys.exit("label loc_00006BE0 not found in 6AC8")
A, p = straight(p, T_A, "A")
PRE = []
while p < len(seq) and seq[p] != ("label", "00006DE0"):
    k, s = seq[p]
    if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or s.startswith("if") or s.startswith("{"):
        sys.exit(f"6AC8 PRE: unexpected control flow: {k} {s}")
    PRE.append(s); p += 1
INNER, p = straight(p + 1, T_INNER, "INNER")
if p >= len(seq) or seq[p] != ("label", "00006F40"):
    sys.exit("6AC8: loc_00006F40 does not follow the inner back edge")
TAIL, p = straight(p + 1, T_OUTER, "TAIL")
if p >= len(seq) or seq[p] != ("label", "00006F98"):
    sys.exit("6AC8: loc_00006F98 does not follow the outer back edge")

# ---- symbolic evaluation ----
ADDR = re.compile(r"^ctx->gpr\[(\d+)\]\._u32\[0\] \+ (?:ctx->gpr\[(\d+)\]\._u32\[0\]|(-?0x[0-9A-Fa-f]+|\d+))$")


def run(block, what, loads_ok, stores_ok):
    env, stores = {}, []

    def reg(n):
        return env.get(n, ("in", n))

    def addr(a):
        m = ADDR.match(a.strip())
        if not m:
            sys.exit(f"6AC8 {what}: unexpected LS address {a}")
        return (reg(int(m.group(1))), reg(int(m.group(2))) if m.group(2) else int(m.group(3), 0))

    def ev(e):
        m = re.match(r"^(spu_\w+)\((.*)\)$", e)
        if not m:
            sys.exit(f"6AC8 {what}: unexpected expression {e}")
        op, args = m.group(1), [a.strip() for a in m.group(2).split(",")]
        if op == "spu_ls_read128":
            if args[0] != "ctx" or not loads_ok:
                sys.exit(f"6AC8 {what}: unexpected read {e}")
            return ("ld", addr(", ".join(args[1:])))
        vals = []
        for a in args:
            r = re.match(r"^ctx->gpr\[(\d+)\]$", a)
            vals.append(reg(int(r.group(1))) if r else int(a, 0))
        if op in ("spu_ori", "spu_shlqbyi") and vals[1] == 0:
            return vals[0]                      # register copies
        return (op,) + tuple(vals)

    for s in block:
        m = ASSIGN.match(s)
        if m:
            env[int(m.group(1))] = ev(m.group(2))
            continue
        w = re.match(r"^spu_ls_write128\(ctx, (.*), ctx->gpr\[(\d+)\]\);$", s)
        if not w or not stores_ok:
            sys.exit(f"unexpected statement in 6AC8 {what}: {s}")
        stores.append((addr(w.group(1)), reg(int(w.group(2)))))
    return env, stores


IN = lambda n: ("in", n)
op = lambda name, *a: ("spu_" + name,) + a
LD = lambda b, o: ("ld", (b, o))
splat = lambda x, c: op("shufb", x, x, IN(c))     # r47/r41/r39/r35: words 0/1/2/3 (checked at run time)


def d2(S, X, Y, Z):
    dx, dy, dz = (op("fs", splat(S, c), V) for c, V in ((47, X), (41, Y), (39, Z)))
    return op("fma", dz, dz, op("fma", dy, dy, op("fm", dx, dx)))


def dist(D):
    y = op("fi", D, op("frsqest", D))
    return op("fm", op("fma", op("fnms", op("fm", y, D), y, IN(29)), op("fm", y, IN(52)), y), D)


def weight(d, R, Vw):
    x = op("fm", d, R)
    t = op("fs", IN(29), op("selb", x, IN(29), op("fcgt", x, IN(29))))
    s = op("fm", op("fm", t, t), op("fnms", IN(66), t, IN(65)))
    return op("fm", op("fm", s, s), Vw)


def transpose(Pa, Pb, Pc, Pd):
    c81 = op("shufb", Pc, Pd, IN(81))
    m1 = op("selb", c81, op("selb", Pa, Pb, IN(56)), IN(54))
    m2 = op("shufb", c81, op("selb", Pa, Pb, IN(54)), IN(82))
    m3 = op("shufb", op("selb", Pb, Pa, IN(54)), op("selb", Pc, Pd, IN(54)), IN(83))
    return op("selb", m1, m2, IN(56)), op("selb", m3, m1, IN(56)), op("selb", m2, m3, IN(56))


def expect(env, want, what):
    for n, x in want.items():
        if env.get(n, IN(n)) != x:
            sys.exit(f"6AC8 {what}: r{n} is not what the packed path was written for")


IDX = [op("rotqbyi", LD(IN(67), 16 * k), 12) for k in range(8)]
P = [LD(IN(48), i) for i in IDX]
XL, YL, ZL = transpose(*P[0:4])
XH, YH, ZH = transpose(*P[4:8])
S0, S1, V0, V1 = LD(IN(51), IN(77)), LD(IN(51), IN(78)), LD(IN(50), IN(77)), LD(IN(50), IN(78))
ACC = (26, 32, 25, 31, 24, 28, 30, 27)   # vertex 0..7: lo lanes 0-3, hi lanes 0-3
OREG = (72, 71, 76, 75, 70, 69, 74, 73)  # LS[r34 + idx_k], vertex 0..7
IREG = (59, 58, 63, 61, 57, 55, 62, 60)  # idx_k
envA, _ = run(A, "A", True, False)
want = {44: XL, 43: XH, 42: YL, 40: YH, 38: ZL, 37: ZH,
        4: dist(d2(S0, XL, YL, ZL)), 3: dist(d2(S0, XH, YH, ZH)),
        21: d2(S1, XL, YL, ZL), 22: d2(S1, XH, YH, ZH),
        20: op("fm", IN(53), V0), 36: op("fm", IN(53), V1),
        18: splat(op("fm", IN(53), V0), 35), 45: splat(op("fm", IN(53), V1), 35),
        33: splat(S0, 35), 46: splat(S1, 35), 19: IN(84), 67: op("ai", IN(67), 128)}
for k in range(8):
    want[ACC[k]] = IN(79)
    want[IREG[k]] = IDX[k]
    want[OREG[k]] = LD(IN(34), IDX[k])
expect(envA, want, "A")
envP, _ = run(PRE, "PRE", False, False)
expect(envP, {15: IN(33), 23: IN(18), 33: op("fsmbi", 0)}, "PRE")
if set(envP) != {15, 23, 33}:
    sys.exit("6AC8 PRE: writes other registers")
envI, _ = run(INNER, "INNER", True, False)
S2, V2 = LD(IN(51), IN(19)), LD(IN(50), IN(19))
wl, wh = weight(IN(4), IN(15), IN(23)), weight(IN(3), IN(15), IN(23))
want = {4: dist(IN(21)), 3: dist(IN(22)),
        21: d2(S2, IN(44), IN(42), IN(38)), 22: d2(S2, IN(43), IN(40), IN(37)),
        20: IN(36), 36: op("fm", IN(53), V2), 23: IN(45), 45: splat(op("fm", IN(53), V2), 35),
        15: IN(46), 46: splat(S2, 35), 19: op("ai", IN(19), 16), 33: op("ai", IN(33), 1),
        18: op("ceq", IN(64), op("ai", IN(33), 1))}
for k, c in enumerate((47, 41, 39, 35)):
    want[ACC[k]] = op("fma", splat(wl, c), IN(20), IN(ACC[k]))
    want[ACC[k + 4]] = op("fma", splat(wh, c), IN(20), IN(ACC[k + 4]))
expect(envI, want, "INNER")
envT, stT = run(TAIL, "TAIL", False, True)
want_st = [((IN(34), IN(IREG[k])), op("fma", IN(OREG[k]), IN(49), IN(ACC[k]))) for k in range(8)]
if stT != want_st:
    sys.exit("6AC8 TAIL: the stores are not the ones the packed path was written for")
expect(envT, {68: op("ai", IN(68), 8), 76: op("cgt", IN(80), op("ai", IN(68), 8))}, "TAIL")

# ---- registers, macros ----
read, written, mentioned = set(), set(), set()
out = {}
for name, blk in (("A", A), ("PRE", PRE), ("INNER", INNER), ("TAIL", TAIL)):
    o = []
    for s in blk:
        m = ASSIGN.match(s)
        rhs = m.group(2) if m else s
        for n in map(int, GPR.findall(rhs)):
            if n not in written:
                read.add(n)
        mentioned |= set(map(int, GPR.findall(s)))
        if m:
            written.add(int(m.group(1)))
        s = s.replace("spu_ls_read128(ctx, ", "LD(").replace("spu_ls_write128(ctx, ", "ST(")
        o.append(GPR.sub(lambda g: f"r{g.group(1)}", s))
    if "ctx" in " ".join(o):
        sys.exit(f"ctx left in the 6AC8 {name} block")
    out[name] = o
read.add(85)   # the inner-loop test
mentioned |= {85, 18, 76}
INPUTS = {29, 34, 35, 39, 41, 47, 48, 49, 50, 51, 52, 53, 54, 56, 64, 65, 66, 67, 68, 77, 78, 79, 80, 81, 82, 83,
          84, 85}
if read != INPUTS:
    sys.exit(f"6AC8: registers read before written {sorted(read)}, want {sorted(INPUTS)}")
if written & (INPUTS - {67, 68}):
    sys.exit(f"6AC8: the nest writes loop invariants {sorted(written & INPUTS)}")
regs = sorted(mentioned)
w = max(len(s) for o in out.values() for s in o)
h = ["/* generated by gen_edge_6ac8_body.py from the d935 lift -- game code, do not commit */",
     "#define GOW_6A_REGS() u128 " + ", ".join(f"r{n}" for n in regs) + ";", "",
     "#define GOW_6A_LOAD() \\"] + [f"    r{n} = g[{n}]; \\" for n in regs] + ["", "",
     "#define GOW_6A_STORE() \\"] + [f"    g[{n}] = r{n}; \\" for n in sorted(written)] + ["", ""]
for name in ("A", "PRE", "INNER", "TAIL"):
    h += [f"#define GOW_6A_{name}() \\"] + [f"    {s.ljust(w)} \\" for s in out[name]] + ["", ""]
open(dst, "w", newline="\n").write("\n".join(h))
print(f"6A: A {len(A)}, PRE {len(PRE)}, INNER {len(INNER)}, TAIL {len(TAIL)} statements; {len(regs)} registers, "
      f"{len(written)} written, inputs {sorted(read)}; dataflow checked -> {dst}")
