"""gen_edge_f538_body.py <spu_recomp.c> <out.h>: the F538 loop macros for port/gow_edge_native_f538.c.

The native F538 stage replaces the loop at loc_0000F620 of the d935 EDGE job module's function F538 (exit:
the fall-through after its back edge, which is F538's return). v1 runs the lifted statements in C locals,
and the packed versions run the last iteration with them. Those statements are game code, so the repo does
not carry them: this script takes them from the d935 module's lift (jobmod_d93503647dd8_e6d98/spu_recomp.c;
the copy in function F538 itself: the lift also has the alternate-entry function F620 with a copy of the
loop, which stays lifted) and writes gow_edge_f538_body.h into the build tree:

  GOW_F53_REGS()   u128 locals for every register the loop mentions
  GOW_F53_LOAD()   rN = g[N] for each of them
  GOW_F53_STORE()  g[N] = rN for every register it writes
  GOW_F53_BODY()   loc_0000F620 up to its back edge `if (r37 != 0) goto loc_0000F620`

The control flow (`do BODY while (r37 != 0)`) is written out in gow_edge_native_f538.c. This script checks
that the lift has exactly that shape (the back edge, then F538's return or the switch's exit label; no other
label, branch, call or return inside), and it evaluates the body symbolically: every load comes before the
first store, and the eight stores (addresses, values and order) and the registers the next iteration reads
must be exactly the dataflow the packed path
was written for (per input quad v at r38 + 16k: fm(v, r35), then fma(v rotated by 4, 8, 12 bytes, r36 /
r33 / r34, the sum), the last fma of quads 2-7 deferred to the next iteration; the stores write the
previous iteration's results at r58/r54/r47/r46/r45/r44/r42/r40 + r39). It fails otherwise. Statements:
ctx->gpr[N] -> rN, spu_ls_read128/write128(ctx, ...) -> LD/ST(...). The port's CMakeLists.txt runs it
whenever the lift changes. A lab lift works as well (a probe wrapper renames a function to _impl, --blocks
adds counters at labels).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, newline="").read().replace("\r\n", "\n").split("\n")
GPR = re.compile(r"ctx->gpr\[(\d+)\]")
ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (.*);$")
BACK = "if (ctx->gpr[37]._u32[0] != 0) goto loc_0000F620;"
EXITS = ("SPU_RET(ctx);", "gow_f620_exit:")


def func(addr):
    hits = [i for i, l in enumerate(lines)
            if re.match(rf"^void \w+_spu_func_{addr}(?:_impl)?\(spu_context\* ctx\) \{{", l)]
    # a probe lift has both the _impl and a wrapper with the plain name: take the one with labels in it
    for fn in hits:
        end = next(i for i in range(fn + 1, len(lines)) if lines[i].startswith("}"))
        if any(re.match(r"^\s*loc_", l) for l in lines[fn:end]):
            return fn, end
    sys.exit(f"function {addr} not found")


fn, end = func("0000F538")
seq, inside = [], False
for l in lines[fn:end]:
    s = re.sub(r"g_fnp_blk\[\d+\]\+\+;", "", l.strip()).strip()   # --blocks probe counters
    m = re.match(r"^loc_([0-9A-F]{8}):\s*(.*)$", s)
    if m:
        if m.group(1) == "0000F620":
            inside = True
        if inside:
            seq.append(("label", m.group(1)))
        s = m.group(2).strip()
    if not inside or not s or s in ("/* branch hint (ignored) */;", "/* nop */;"):
        continue
    seq.append(("stmt", s))
if not seq or seq[0] != ("label", "0000F620"):
    sys.exit("label loc_0000F620 not found in F538")
pos = 1
body = []
while pos < len(seq) and seq[pos] != ("stmt", BACK):
    k, s = seq[pos]
    if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or "SPU_RET" in s or "pc =" in s \
            or s.startswith("if") or s.startswith("{"):
        sys.exit(f"F620 body: unexpected control flow: {k} {s}")
    body.append(s); pos += 1
if pos == len(seq):
    sys.exit("F620: back edge not found")
if pos + 1 >= len(seq) or seq[pos + 1][0] != "stmt" or seq[pos + 1][1] not in EXITS:
    sys.exit(f"F620: the return does not follow the back edge: {seq[pos + 1] if pos + 1 < len(seq) else None}")

# ---- symbolic evaluation of one iteration ----
ARG = re.compile(r"^ctx->gpr\[(\d+)\]$")
ADDR = re.compile(r"^ctx->gpr\[(\d+)\]\._u32\[0\] \+ (?:ctx->gpr\[(\d+)\]\._u32\[0\]|(0x[0-9A-Fa-f]+|\d+))$")
env, stores = {}, []


def reg(n):
    return env.get(n, ("in", n))


def addr(a):
    m = ADDR.match(a.strip())
    if not m:
        sys.exit(f"F620: unexpected LS address {a}")
    return ("addr", reg(int(m.group(1))), reg(int(m.group(2))) if m.group(2) else int(m.group(3), 0))


def ev(e):
    m = re.match(r"^(spu_\w+)\((.*)\)$", e)
    if not m:
        sys.exit(f"F620: unexpected expression {e}")
    op, args = m.group(1), [a.strip() for a in m.group(2).split(",")]
    if op == "spu_ls_read128":
        if args[0] != "ctx":
            sys.exit(f"F620: unexpected read {e}")
        if stores:   # the packed path does all of an iteration's loads before its stores
            sys.exit(f"F620: a load after a store: {e}")
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
        sys.exit(f"unexpected statement in the F620 body: {s}")
    stores.append((addr(w.group(1)), reg(int(w.group(2)))))

IN = lambda n: ("in", n)
M = [IN(35), IN(36), IN(33), IN(34)]
V = [("ld", ("addr", IN(38), 16 * k)) for k in range(8)]


def part(v, upto):   # fm(v, M0), then fma(rot(v, 4i), Mi, sum) for i = 1 .. upto - 1
    x = ("spu_fm", v, M[0])
    for i in range(1, upto):
        x = ("spu_fma", ("spu_rotqbyi", v, 4 * i), M[i], x)
    return x


# stores: the previous iteration's results, in this order
want_st = [(("addr", IN(58), IN(39)), IN(20)), (("addr", IN(54), IN(39)), IN(21))]
for k, (p, r, base) in enumerate(((9, 53, 47), (43, 52, 46), (41, 51, 45), (50, 57, 44), (49, 56, 42),
                                  (48, 55, 40))):
    want_st.append((("addr", IN(base), IN(39)), ("spu_fma", IN(r), M[3], IN(p))))
if stores != want_st:
    sys.exit("F620: the stores are not the ones the packed path was written for")
# carried into the next iteration: quads 0/1 complete, quads 2-7 three ops and the operand of the fourth
want = {20: part(V[0], 4), 21: part(V[1], 4), 38: ("spu_ai", IN(38), 128), 37: ("spu_ai", IN(37), -1),
        39: ("spu_a", IN(39), IN(32)), 32: ("spu_il", 128)}
for k, (p, r) in enumerate(((9, 53), (43, 52), (41, 51), (50, 57), (49, 56), (48, 55))):
    want[p] = part(V[k + 2], 3)
    want[r] = ("spu_rotqbyi", V[k + 2], 12)
for n, x in want.items():
    if env.get(n) != x:
        sys.exit(f"F620: r{n} at the back edge is not what the packed path was written for")

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
    sys.exit("ctx left in the F620 body")
INPUTS = {9, 20, 21, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
          54, 55, 56, 57, 58}
if read != INPUTS:
    sys.exit(f"F620: registers read before written {sorted(read)}, want {sorted(INPUTS)}")
regs = sorted(mentioned)
w = max(len(s) for s in out)
h = ["/* generated by gen_edge_f538_body.py from the d935 lift -- game code, do not commit */",
     "#define GOW_F53_REGS() u128 " + ", ".join(f"r{n}" for n in regs) + ";", "",
     "#define GOW_F53_LOAD() \\"] + [f"    r{n} = g[{n}]; \\" for n in regs] + ["", "",
     "#define GOW_F53_STORE() \\"] + [f"    g[{n}] = r{n}; \\" for n in sorted(written)] + ["", "",
     "#define GOW_F53_BODY() \\"] + [f"    {s.ljust(w)} \\" for s in out] + ["", ""]
open(dst, "w", newline="\n").write("\n".join(h))
print(f"F53: {len(out)} statements; {len(regs)} registers, {len(written)} written, inputs {sorted(read)}; "
      f"dataflow checked -> {dst}")
