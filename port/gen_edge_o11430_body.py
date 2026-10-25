"""gen_edge_o11430_body.py <spu_recomp.c> <out.h>: the 11430 loop macros for port/gow_edge_native_o11430.c.

The native O11430 stage replaces the loop at loc_00011460 of function 11430 in ONE EDGE overlay fragment
lift, edgefrag_7228c0_11150 (overlay 0x7228C0, entry 0x11150; exit: the fall-through after the back edge,
then 11430's return). v1 runs the lifted statements in C locals, and the packed versions run the last
iteration with them. Those statements are game code, so the repo does not carry them: this script takes
them from the fragment's lift (spu/edgefrag_7228c0_11150/spu_recomp.c: the copy in function 11430 itself;
the lift also has the alternate-entry function 11460 with a copy of the loop, which stays lifted) and
writes gow_edge_o11430_body.h into the build tree:

  GOW_O1430_REGS()   u128 locals for every register the loop mentions
  GOW_O1430_LOAD()   rN = g[N] for each of them
  GOW_O1430_STORE()  g[N] = rN for every register it writes
  GOW_O1430_BODY()   loc_00011460 up to its back edge `if (r19 != 0) goto loc_00011460`

The control flow (`do BODY while (r19 != 0)`) is written out in gow_edge_native_o11430.c. This script checks
that the lift has exactly that shape (the back edge, then `r3 = il(0)` or the switch's exit label; no other
label, branch, call or return inside), and it evaluates the body symbolically: both stores come before the
first load, and the two stores (addresses, values, order) and the registers the next iteration reads must
be exactly the dataflow the packed path was written for:
  - store r36 + r18: selb(fa(csflt(rotma(y9, r17), 155), cuflt(shl(y9, r34), 123)), r35, r38),
    y9 = and(r9, r33) + r31; then store r29 + r18: the same of r5;
  - r9' / r5' = the fields of the record pair held in r13:r2: window shufb(r13, r2, r25) / (r39), shifted
    right per word (rotqmbi then rotqmbybi) by r24/r24, r26/r26, r23/r23, r23/r28 and merged by
    selb(selb(s0, s1, r32), selb(s2, s3, r32), r37);
  - r13' / r2' = shufb(LS[r27], LS[r27 + 16], r67) / shufb(LS[r27 + 16], LS[r27 + 32], r67) with
    r67 = r41 + shufb(r27 & 15, r27 & 15, r40) per byte;
  - r27' = r27 + 24, r19' = r18' = r19 + 32.
It fails otherwise. Statements: ctx->gpr[N] -> rN, spu_ls_read128/write128(ctx, ...) -> LD/ST(...). The port's
CMakeLists.txt runs it whenever the lift changes. A lab lift works as well (a probe wrapper renames a
function to _impl, --blocks adds counters at labels), and so does a lift that already has the switch.
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, newline="").read().replace("\r\n", "\n").split("\n")
GPR = re.compile(r"ctx->gpr\[(\d+)\]")
ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (.*);$")
FN = "edgefrag_7228c0_11150_spu_func_00011430"
BACK = "if (ctx->gpr[19]._u32[0] != 0) goto loc_00011460;"
EXITS = ("ctx->gpr[3] = spu_il(0);", "gow_11460_exit:")


def func():
    hits = [i for i, l in enumerate(lines) if re.match(rf"^void {FN}(?:_impl)?\(spu_context\* ctx\) \{{", l)]
    # a probe lift has both the _impl and a wrapper with the plain name: take the one with labels in it
    for fn in hits:
        end = next(i for i in range(fn + 1, len(lines)) if lines[i].startswith("}"))
        if any(re.match(r"^\s*loc_", l) for l in lines[fn:end]):
            return fn, end
    sys.exit(f"{FN} not found (is this the edgefrag_7228c0_11150 lift?)")


fn, end = func()
seq, inside = [], False
for l in lines[fn:end]:
    s = re.sub(r"g_fnp_blk\[\d+\]\+\+;", "", l.strip()).strip()   # --blocks probe counters
    m = re.match(r"^loc_([0-9A-F]{8}):\s*(.*)$", s)
    if m:
        if m.group(1) == "00011460":
            inside = True
        if inside:
            seq.append(("label", m.group(1)))
        s = m.group(2).strip()
    if not inside or not s or s in ("/* branch hint (ignored) */;", "/* nop */;"):
        continue
    seq.append(("stmt", s))
if not seq or seq[0] != ("label", "00011460"):
    sys.exit("label loc_00011460 not found in 11430")
pos = 1
body = []
while pos < len(seq) and seq[pos] != ("stmt", BACK):
    k, s = seq[pos]
    if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or "SPU_RET" in s or "pc =" in s \
            or s.startswith("if") or s.startswith("{"):
        sys.exit(f"11460 body: unexpected control flow: {k} {s}")
    body.append(s); pos += 1
if pos == len(seq):
    sys.exit("11460: back edge not found")
if pos + 1 >= len(seq) or seq[pos + 1][0] != "stmt" or seq[pos + 1][1] not in EXITS:
    sys.exit(f"11460: the exit does not follow the back edge: {seq[pos + 1] if pos + 1 < len(seq) else None}")

# ---- symbolic evaluation of one iteration ----
ARG = re.compile(r"^ctx->gpr\[(\d+)\]$")
ADDR = re.compile(r"^ctx->gpr\[(\d+)\]\._u32\[0\] \+ (?:ctx->gpr\[(\d+)\]\._u32\[0\]|(0x[0-9A-Fa-f]+|\d+))$")
env, stores, loads = {}, [], 0


def reg(n):
    return env.get(n, ("in", n))


def addr(a):
    m = ADDR.match(a.strip())
    if not m:
        sys.exit(f"11460: unexpected LS address {a}")
    return ("addr", reg(int(m.group(1))), reg(int(m.group(2))) if m.group(2) else int(m.group(3), 0))


def ev(e):
    global loads
    m = re.match(r"^(spu_\w+)\((.*)\)$", e)
    if not m:
        sys.exit(f"11460: unexpected expression {e}")
    op, args = m.group(1), [a.strip() for a in m.group(2).split(",")]
    if op == "spu_ls_read128":
        if args[0] != "ctx":
            sys.exit(f"11460: unexpected read {e}")
        loads += 1
        return ("ld", addr(", ".join(args[1:])))
    vals = []
    for a in args:
        r = ARG.match(a)
        vals.append(reg(int(r.group(1))) if r else int(a, 0))
    if op in ("spu_ori", "spu_shlqbyi") and vals[1] == 0:
        return vals[0]                      # register copies
    return (op,) + tuple(vals)


for s in body:
    m = ASSIGN.match(s)
    if m:
        env[int(m.group(1))] = ev(m.group(2))
        continue
    w = re.match(r"^spu_ls_write128\(ctx, (.*), ctx->gpr\[(\d+)\]\);$", s)
    if not w:
        sys.exit(f"unexpected statement in the 11460 body: {s}")
    if loads:   # the packed path stores an iteration's two quads before the record loads, as the lift does
        sys.exit(f"11460: a store after a load: {s}")
    stores.append((addr(w.group(1)), reg(int(w.group(2)))))

IN = lambda n: ("in", n)


def conv(p):   # the fixed-point decode of one field quad
    y = ("spu_a", ("spu_and", p, IN(33)), IN(31))
    return ("spu_selb", ("spu_fa", ("spu_csflt", ("spu_rotma", y, IN(17)), 155),
                         ("spu_cuflt", ("spu_shl", y, IN(34)), 123)), IN(35), IN(38))


def fields(ctl):   # the four shifted copies of one record's window, merged per word
    w = ("spu_shufb", IN(13), IN(2), IN(ctl))
    sh = lambda b, B: ("spu_rotqmbybi", ("spu_rotqmbi", w, IN(b)), IN(B))
    return ("spu_selb", ("spu_selb", sh(24, 24), sh(26, 26), IN(32)),
            ("spu_selb", sh(23, 23), sh(23, 28), IN(32)), IN(37))


want_st = [(("addr", IN(36), IN(18)), conv(IN(9))), (("addr", IN(29), IN(18)), conv(IN(5)))]
if stores != want_st:
    sys.exit("11460: the stores are not the ones the packed path was written for")
z = ("spu_andbi", IN(27), 15)
r67 = ("spu_a", IN(41), ("spu_shufb", z, z, IN(40)))
ld = lambda o: ("ld", ("addr", IN(27), o))
want = {9: fields(25), 5: fields(39),
        13: ("spu_shufb", ld(0), ld(16), r67), 2: ("spu_shufb", ld(16), ld(32), r67),
        27: ("spu_ai", IN(27), 24), 19: ("spu_ai", IN(19), 32), 18: ("spu_ai", IN(19), 32)}
for n, x in want.items():
    if env.get(n) != x:
        sys.exit(f"11460: r{n} at the back edge is not what the packed path was written for")

read, written, mentioned, out = set(), set(), set(), []
for s in body:
    m = ASSIGN.match(s)
    rhs = m.group(2) if m else s
    for n in map(int, GPR.findall(rhs)):
        if n not in written:
            read.add(n)
    mentioned |= set(map(int, GPR.findall(s)))
    if m:
        written.add(int(m.group(1)))
    s = s.replace("spu_ls_read128(ctx, ", "LD(").replace("spu_ls_write128(ctx, ", "ST(")
    out.append(GPR.sub(lambda g: f"r{g.group(1)}", s))
if "ctx" in " ".join(out):
    sys.exit("ctx left in the 11460 body")
INPUTS = {2, 5, 9, 13, 17, 18, 19, 23, 24, 25, 26, 27, 28, 29, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41}
if read != INPUTS:
    sys.exit(f"11460: registers read before written {sorted(read)}, want {sorted(INPUTS)}")
regs = sorted(mentioned)
w = max(len(s) for s in out)
h = ["/* generated by gen_edge_o11430_body.py from the edgefrag_7228c0_11150 lift -- game code, do not commit */",
     "#define GOW_O1430_REGS() u128 " + ", ".join(f"r{n}" for n in regs) + ";", "",
     "#define GOW_O1430_LOAD() \\"] + [f"    r{n} = g[{n}]; \\" for n in regs] + ["", "",
     "#define GOW_O1430_STORE() \\"] + [f"    g[{n}] = r{n}; \\" for n in sorted(written)] + ["", "",
     "#define GOW_O1430_BODY() \\"] + [f"    {s.ljust(w)} \\" for s in out] + ["", ""]
open(dst, "w", newline="\n").write("\n".join(h))
print(f"O1430: {len(out)} statements; {len(regs)} registers, {len(written)} written, inputs {sorted(read)}; "
      f"dataflow checked -> {dst}")
