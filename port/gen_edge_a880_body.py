"""gen_edge_a880_body.py <spu_recomp.c> <out.h>: the A880 loop macros for port/gow_edge_native_a880.c.

The native A880 stage replaces the loop at loc_0000A950 of the d935 EDGE job module's function A880 (exit
loc_0000AD48). v1 runs the lifted statements in C locals, and the packed versions run the last iteration
with them. Those statements are game code, so the repo does not carry them: this script takes them from the
d935 module's lift (jobmod_d93503647dd8_e6d98/spu_recomp.c; the copy in function A880 itself: the lift also
has alternate-entry functions A928, A950, AD48, AD4C and AD80 with copies of these blocks, which stay
lifted) and writes gow_edge_a880_body.h into the build tree:

  GOW_A88_REGS()   u128 locals for every register the loop mentions
  GOW_A88_LOAD()   rN = g[N] for each of them
  GOW_A88_STORE()  g[N] = rN for every register it writes
  GOW_A88_BODY()   loc_0000A950 up to its back edge `if (r46 != 0) goto loc_0000A950`

The control flow (`do BODY while (r46 != 0)`) is written out in gow_edge_native_a880.c. This script checks
that the lift has exactly that shape (the back edge, then loc_0000AD48; no other label, branch, call or
return inside), that the loop reads no register before writing it except the pointers, counters and the
shuffle control the packed path takes from the registers (r47 r48 r49 r50 r51 r52 r53 r81 r82 r83), and
that its operation counts are the ones the packed path was written for; it fails otherwise. Statements:
ctx->gpr[N] -> rN, spu_ls_read128/write128(ctx, ...) -> LD/ST(...). The port's CMakeLists.txt runs it
whenever the lift changes. A lab lift works as well (a probe wrapper renames a function to _impl, --blocks
adds counters at labels).
"""
import re
import sys
from collections import Counter

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, newline="").read().replace("\r\n", "\n").split("\n")
GPR = re.compile(r"ctx->gpr\[(\d+)\]")
ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (.*);$")
BACK = "if (ctx->gpr[46]._u32[0] != 0) goto loc_0000A950;"
INPUTS = {47, 48, 49, 50, 51, 52, 53, 81, 82, 83}
# per iteration, 8 elements: 6 fm, 2 fnms, 2 fa, 1 fma, frsqest, fi, fcgt, selb, 7 shufb each, ...
OPS = {"spu_ai": 5, "spu_and": 24, "spu_cgt": 1, "spu_fa": 16, "spu_fcgt": 8, "spu_fi": 8, "spu_fm": 48,
       "spu_fma": 8, "spu_fnms": 16, "spu_frsqest": 8, "spu_fsmbi": 1, "spu_ila": 1, "spu_ilhu": 3,
       "spu_iohl": 1, "spu_ls_read128": 18, "spu_ls_write128": 8, "spu_rotqbyi": 8, "spu_selb": 8,
       "spu_shufb": 56}


def func(addr):
    hits = [i for i, l in enumerate(lines)
            if re.match(rf"^void \w+_spu_func_{addr}(?:_impl)?\(spu_context\* ctx\) \{{", l)]
    # a probe lift has both the _impl and a wrapper with the plain name: take the one with labels in it
    for fn in hits:
        end = next(i for i in range(fn + 1, len(lines)) if lines[i].startswith("}"))
        if any(re.match(r"^\s*loc_", l) for l in lines[fn:end]):
            return fn, end
    sys.exit(f"function {addr} not found")


fn, end = func("0000A880")
seq, inside = [], False
for l in lines[fn:end]:
    s = re.sub(r"g_fnp_blk\[\d+\]\+\+;", "", l.strip()).strip()   # --blocks probe counters
    m = re.match(r"^loc_([0-9A-F]{8}):\s*(.*)$", s)
    if m:
        if m.group(1) == "0000A950":
            inside = True
        if inside:
            seq.append(("label", m.group(1)))
        s = m.group(2).strip()
    if not inside or not s or s in ("/* branch hint (ignored) */;", "/* nop */;"):
        continue
    seq.append(("stmt", s))
if not seq or seq[0] != ("label", "0000A950"):
    sys.exit("label loc_0000A950 not found in A880")
pos = 1
body = []
while pos < len(seq) and seq[pos] != ("stmt", BACK):
    k, s = seq[pos]
    if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or "SPU_RET" in s or "pc =" in s \
            or s.startswith("if") or s.startswith("{"):
        sys.exit(f"A950 body: unexpected control flow: {k} {s}")
    body.append(s); pos += 1
if pos == len(seq):
    sys.exit("A950: back edge not found")
if pos + 1 >= len(seq) or seq[pos + 1] != ("label", "0000AD48"):
    sys.exit(f"A950: loc_0000AD48 does not follow the back edge: {seq[pos + 1] if pos + 1 < len(seq) else None}")

ops = Counter(re.findall(r"spu_\w+", " ".join(body)))
if dict(ops) != OPS:
    sys.exit("A950: operation counts differ from the ones the packed path was written for: " +
             ", ".join(f"{k} {ops.get(k, 0)} (want {OPS.get(k, 0)})" for k in sorted(set(ops) | set(OPS))
                       if ops.get(k, 0) != OPS.get(k, 0)))
read, written, mentioned, out = set(), set(), set(), []
for s in body:
    m = ASSIGN.match(s)
    if not m and not s.startswith("spu_ls_write128(ctx,"):
        sys.exit(f"unexpected statement in the A950 body: {s}")
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
    sys.exit("ctx left in the A950 body")
if read != INPUTS:
    sys.exit(f"A950: registers read before written {sorted(read)}, want {sorted(INPUTS)}")
if 46 not in written:
    sys.exit("A950: the branch register r46 is not written in the body")
regs = sorted(mentioned)
w = max(len(s) for s in out)
h = ["/* generated by gen_edge_a880_body.py from the d935 lift -- game code, do not commit */",
     "#define GOW_A88_REGS() u128 " + ", ".join(f"r{n}" for n in regs) + ";", "",
     "#define GOW_A88_LOAD() \\"] + [f"    r{n} = g[{n}]; \\" for n in regs] + ["", "",
     "#define GOW_A88_STORE() \\"] + [f"    g[{n}] = r{n}; \\" for n in sorted(written)] + ["", "",
     "#define GOW_A88_BODY() \\"] + [f"    {s.ljust(w)} \\" for s in out] + ["", ""]
open(dst, "w", newline="\n").write("\n".join(h))
print(f"A88: {len(out)} statements; {len(regs)} registers, {len(written)} written, inputs {sorted(read)} -> {dst}")
