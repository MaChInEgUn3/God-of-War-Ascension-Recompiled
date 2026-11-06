"""gen_edge_dcc0_body.py <spu_recomp.c> <out.h>: the DCC0 region macros for port/gow_edge_native_dcc0.c.

The native DCC0 stage replaces the region of the d935 EDGE job module's function DCC0 from loc_0000E210 to
the back edge of its last loop: loop E210, the straight-line block after it, loop E30C, the block after it
and loop E3C4 (exit: the fall-through after E3C4's back edge; the three statements after it and the tail call
to B600 stay lifted). It runs the lifted statements in C locals. Those statements are game code, so the repo
does not carry them: this script takes them from the d935 module's lift (jobmod_d93503647dd8_e6d98/
spu_recomp.c; the copy in function DCC0 itself: the lift also has the alternate-entry functions E210, E30C
and E3C4 with copies of the loops, which stay lifted) and writes gow_edge_dcc0_body.h into the build tree:

  GOW_DC_REGS()    u128 locals for every register the region mentions
  GOW_DC_LOAD()    rN = g[N] for each of them
  GOW_DC_STORE()   g[N] = rN for every register it writes
  GOW_DC_L1()      loc_0000E210 up to its back edge `if (r19 != 0) goto loc_0000E210`
  GOW_DC_M1()      the block between that back edge and loc_0000E30C
  GOW_DC_L2()      loc_0000E30C up to its back edge `if (r31 != 0) goto loc_0000E30C`
  GOW_DC_M2()      the block between that back edge and loc_0000E3C4
  GOW_DC_L3()      loc_0000E3C4 up to its back edge `if (r21 != 0) goto loc_0000E3C4`

The control flow (three do-while loops with the blocks between them) is written out in
gow_edge_native_dcc0.c. This script checks that the lift has exactly that shape: the three labels in that
order, each loop closed by its back edge on word 0 of that register, no other label, branch, call, return or
channel access inside, every statement an assignment of an spu_ op to a register or a 128-bit LS store. It
fails otherwise. Statements: ctx->gpr[N] -> rN, spu_ls_read128/write128(ctx, ...) -> LD/ST(...). The port's
CMakeLists.txt runs it whenever the lift changes. A lab lift works as well (a probe wrapper renames a function
to _impl, --blocks adds counters at labels).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, newline="").read().replace("\r\n", "\n").split("\n")
GPR = re.compile(r"ctx->gpr\[(\d+)\]")
ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (spu_\w+\(.*\));$")
STORE = re.compile(r"^spu_ls_write128\(ctx, (.*), ctx->gpr\[(\d+)\]\);$")
LOOPS = (("0000E210", 19), ("0000E30C", 31), ("0000E3C4", 21))


def back(label, r):
    return f"if (ctx->gpr[{r}]._u32[0] != 0) goto loc_{label};"


def func(addr):
    hits = [i for i, l in enumerate(lines)
            if re.match(rf"^void \w+_spu_func_{addr}(?:_impl)?\(spu_context\* ctx\) \{{", l)]
    # a probe lift has both the _impl and a wrapper with the plain name: take the one with labels in it
    for fn in hits:
        end = next(i for i in range(fn + 1, len(lines)) if lines[i].startswith("}"))
        if any(re.match(r"^\s*loc_", l) for l in lines[fn:end]):
            return fn, end
    sys.exit(f"function {addr} not found")


fn, end = func("0000DCC0")
seq, inside = [], False
for l in lines[fn:end]:
    s = re.sub(r"g_fnp_blk\[\d+\]\+\+;", "", l.strip()).strip()   # --blocks probe counters
    m = re.match(r"^loc_([0-9A-F]{8}):\s*(.*)$", s)
    if m:
        if m.group(1) == LOOPS[0][0]:
            inside = True
        if inside:
            seq.append(("label", m.group(1)))
        s = m.group(2).strip()
    if not inside or not s or s in ("/* branch hint (ignored) */;", "/* nop */;"):
        continue
    seq.append(("stmt", s))
    if s == back(*LOOPS[-1]):
        break
if not seq or seq[0] != ("label", LOOPS[0][0]):
    sys.exit("label loc_0000E210 not found in DCC0")

# split: L1, M1, L2, M2, L3 (a loop: its label, its statements, its back edge)
blocks, pos = [], 0
for i, (label, r) in enumerate(LOOPS):
    if pos >= len(seq) or seq[pos] != ("label", label):
        sys.exit(f"DCC0: expected loc_{label} at this point, found {seq[pos] if pos < len(seq) else None}")
    pos += 1
    body = []
    while pos < len(seq) and seq[pos] != ("stmt", back(label, r)):
        body.append(seq[pos]); pos += 1
    if pos == len(seq):
        sys.exit(f"DCC0: back edge of loc_{label} not found")
    pos += 1
    blocks.append((f"L{i + 1}", body))
    if i + 1 < len(LOOPS):
        mid = []
        while pos < len(seq) and seq[pos][0] == "stmt":
            mid.append(seq[pos]); pos += 1
        blocks.append((f"M{i + 1}", mid))
if pos != len(seq):
    sys.exit(f"DCC0: unexpected trailing items {seq[pos:pos + 3]}")

read, written, mentioned, out = set(), set(), set(), {}
n_ld = n_st = 0
for name, body in blocks:
    ob = []
    for k, s in body:
        if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or "SPU_" in s or "pc =" in s \
                or s.startswith("if") or s.startswith("{") or "channel" in s or "rdch" in s or "wrch" in s:
            sys.exit(f"DCC0 {name}: unexpected control flow or side effect: {k} {s}")
        m, w = ASSIGN.match(s), STORE.match(s)
        if not m and not w:
            sys.exit(f"DCC0 {name}: unexpected statement {s}")
        rhs = m.group(2) if m else s
        if "ctx" in GPR.sub("", rhs).replace("spu_ls_read128(ctx, ", "").replace("spu_ls_write128(ctx, ", ""):
            sys.exit(f"DCC0 {name}: ctx used other than for GPRs and LS: {s}")
        for n in map(int, GPR.findall(rhs)):
            if n not in written:
                read.add(n)
        mentioned |= set(map(int, GPR.findall(s)))
        if m:
            written.add(int(m.group(1)))
        n_ld += s.count("spu_ls_read128(")
        n_st += 1 if w else 0
        s = s.replace("spu_ls_read128(ctx, ", "LD(").replace("spu_ls_write128(ctx, ", "ST(")
        ob.append(GPR.sub(lambda g: f"r{g.group(1)}", s))
    out[name] = ob
# the loop conditions read registers too
for label, r in LOOPS:
    mentioned.add(r)
if any("ctx" in s for ob in out.values() for s in ob):
    sys.exit("ctx left in the DCC0 region")
regs = sorted(mentioned)
w = max(len(s) for ob in out.values() for s in ob)
h = ["/* generated by gen_edge_dcc0_body.py from the d935 lift -- game code, do not commit */",
     "#define GOW_DC_REGS() u128 " + ", ".join(f"r{n}" for n in regs) + ";", "",
     "#define GOW_DC_LOAD() \\"] + [f"    r{n} = g[{n}]; \\" for n in regs] + ["", "",
     "#define GOW_DC_STORE() \\"] + [f"    g[{n}] = r{n}; \\" for n in sorted(written)] + ["", ""]
for name, _ in blocks:
    h += [f"#define GOW_DC_{name}() \\"] + [f"    {s.ljust(w)} \\" for s in out[name]] + ["    ;", "", ""]
h += [f"#define GOW_DC_COND{i + 1} r{r}._u32[0]" for i, (_, r) in enumerate(LOOPS)] + [""]
open(dst, "w", newline="\n").write("\n".join(h))
print(f"DC: " + ", ".join(f"{k} {len(v)}" for k, v in out.items()) + f" statements ({n_ld} loads, {n_st} stores); "
      f"{len(regs)} registers, {len(written)} written, {len(read)} read before written; shape checked -> {dst}")
