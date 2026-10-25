"""gen_edge_d640_body.py <spu_recomp.c> <out.h>: the D640/D250 loop macros for port/gow_edge_native_d640.c.

The native D640 stage replaces two loops of the d935 EDGE job module: D640's loop at loc_0000D92C (exit
loc_0000DB70) and the loop at loc_0000D4DC in D250 (D640's only callee there; exit: the fall-through after
its back edge). v1 runs the lifted statements in C locals, and the packed versions run the last iteration
of each loop with them. Those statements are game code, so the repo does not carry them: this script takes
them from the d935 module's lift (jobmod_d93503647dd8_e6d98/spu_recomp.c; the copies in functions D640 and
D250 themselves: the lift also has alternate-entry functions D92C, D8C8 and D4DC with copies of these
blocks, which stay lifted) and writes gow_edge_d640_body.h into the build tree:

  GOW_D6A_REGS()   u128 locals for every register the D92C loop mentions
  GOW_D6A_LOAD()   rN = g[N] for each of them
  GOW_D6A_STORE()  g[N] = rN for every register it writes
  GOW_D6A_BODY()   loc_0000D92C up to the exit test `if (r2 == 0) goto loc_0000DB70`
  GOW_D6A_TAIL()   the statements from there to the back edge `goto loc_0000D92C`
  GOW_D6B_REGS() / GOW_D6B_LOAD() / GOW_D6B_STORE()   the same for the D4DC loop
  GOW_D6B_BODY()   loc_0000D4DC up to its back edge `if (r3 != 0) goto loc_0000D4DC`

The control flow (`for (;;) { BODY; if (r2 == 0) break; TAIL; }` and `do BODY while (r3 != 0)`) is written
out in gow_edge_native_d640.c; this script checks that the lift has exactly that shape (those branch lines,
no other label, branch, call or return inside) and fails otherwise. Statements: ctx->gpr[N] -> rN,
spu_ls_read128/write128(ctx, ...) -> LD/ST(...). The port's CMakeLists.txt runs it whenever the lift changes.
A lab lift works as well (a probe wrapper renames a function to _impl, --blocks adds counters at labels).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, newline="").read().replace("\r\n", "\n").split("\n")
GPR = re.compile(r"ctx->gpr\[(\d+)\]")
ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (.*);$")


def func(addr):
    hits = [i for i, l in enumerate(lines)
            if re.match(rf"^void \w+_spu_func_{addr}(?:_impl)?\(spu_context\* ctx\) \{{", l)]
    # a probe lift has both the _impl and a wrapper with the plain name: take the one with labels in it
    for fn in hits:
        end = next(i for i in range(fn + 1, len(lines)) if lines[i].startswith("}"))
        if any(re.match(r"^\s*loc_", l) for l in lines[fn:end]):
            return fn, end
    sys.exit(f"function {addr} not found")


def items(fn, end, start):
    """(kind, text) from label `start` to the end of the function"""
    seq, inside = [], False
    for l in lines[fn:end]:
        s = re.sub(r"g_fnp_blk\[\d+\]\+\+;", "", l.strip()).strip()   # --blocks probe counters
        m = re.match(r"^loc_([0-9A-F]{8}):\s*(.*)$", s)
        if m:
            if m.group(1) == start:
                inside = True
            if inside:
                seq.append(("label", m.group(1)))
            s = m.group(2).strip()
        if not inside or not s or s in ("/* branch hint (ignored) */;", "/* nop */;"):
            continue
        seq.append(("stmt", s))
    if not inside:
        sys.exit(f"label loc_{start} not found")
    return seq


def plain(name, body):
    for k, s in body:
        if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or "SPU_RET" in s or "pc =" in s \
                or s.startswith("if") or s.startswith("{"):
            sys.exit(f"{name}: unexpected control flow: {k} {s}")
    return [s for _, s in body]


def take(seq, pos, term, name):
    body = []
    while pos < len(seq) and seq[pos] != term:
        body.append(seq[pos]); pos += 1
    if pos == len(seq):
        sys.exit(f"{name}: {term} not found")
    return plain(name, body), pos + 1


G = lambda r, op, lab: ("stmt", f"if (ctx->gpr[{r}]._u32[0] {op} 0) goto loc_{lab};")
# D640: loc_D92C: BODY; if (r2 == 0) goto DB70; TAIL; goto D92C; loc_DB70:
fn, end = func("0000D640")
seq = items(fn, end, "0000D92C")
assert seq[0] == ("label", "0000D92C")
a_body, pos = take(seq, 1, G(2, "==", "0000DB70"), "D92C body")
a_tail, pos = take(seq, pos, ("stmt", "goto loc_0000D92C;"), "D92C tail")
if seq[pos] != ("label", "0000DB70"):
    sys.exit(f"D92C: loc_0000DB70 does not follow the back edge: {seq[pos]}")
# D250: loc_D4DC: BODY; if (r3 != 0) goto D4DC;  (then the epilogue)
fn, end = func("0000D250")
seq = items(fn, end, "0000D4DC")
assert seq[0] == ("label", "0000D4DC")
b_body, pos = take(seq, 1, G(3, "!=", "0000D4DC"), "D4DC body")
if any(k == "label" for k, _ in seq[pos:]):
    sys.exit("D4DC: a label after the back edge")


def conv(name, stmts, mentioned, written):
    o = []
    for s in stmts:
        m = ASSIGN.match(s)
        if not m and not s.startswith("spu_ls_write128(ctx,"):
            sys.exit(f"unexpected statement in {name}: {s}")
        mentioned |= set(map(int, GPR.findall(s)))
        if m:
            written.add(int(m.group(1)))
        s = s.replace("spu_ls_read128(ctx, ", "LD(").replace("spu_ls_write128(ctx, ", "ST(")
        o.append(GPR.sub(lambda g: f"r{g.group(1)}", s))
    if "ctx" in " ".join(o):
        sys.exit(f"ctx left in {name}")
    return o


h = ["/* generated by gen_edge_d640_body.py from the d935 lift -- game code, do not commit */"]
for pre, segs in (("D6A", (("BODY", a_body), ("TAIL", a_tail))), ("D6B", (("BODY", b_body),))):
    mentioned, written, out = set(), set(), {}
    for name, st in segs:
        out[name] = conv(f"{pre} {name}", st, mentioned, written)
    mentioned |= {2} if pre == "D6A" else {3}   # the branch registers
    regs = sorted(mentioned)
    w = max(len(s) for o in out.values() for s in o)
    h += [f"#define GOW_{pre}_REGS() u128 " + ", ".join(f"r{n}" for n in regs) + ";", "",
          f"#define GOW_{pre}_LOAD() \\"] + [f"    r{n} = g[{n}]; \\" for n in regs] + ["", "",
          f"#define GOW_{pre}_STORE() \\"] + [f"    g[{n}] = r{n}; \\" for n in sorted(written)] + ["", ""]
    for name, o in out.items():
        h += [f"#define GOW_{pre}_{name}() \\"] + [f"    {s.ljust(w)} \\" for s in o] + ["", ""]
    print(f"{pre}: " + ", ".join(f"{k} {len(v)}" for k, v in out.items()) +
          f" statements; {len(regs)} registers, {len(written)} written")
open(dst, "w", newline="\n").write("\n".join(h))
print(f"-> {dst}")
