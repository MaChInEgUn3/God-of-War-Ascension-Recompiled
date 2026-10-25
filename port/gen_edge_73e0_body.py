"""gen_edge_73e0_body.py <spu_recomp.c> <out.h>: the 73E0 region macros for port/gow_edge_native_73e0.c.

The native 73E0 region (the outer loop at loc_000079B0 with its two inner loops at loc_00007BB4 and
loc_00007ED8, up to loc_000081C0) runs the lifted statements in v1, and v2 runs the outer-loop parts and the
last iteration of each inner loop with them. Those statements are game code, so the repo does not carry them:
this script takes them from the d935 module's lift (jobmod_d93503647dd8_e6d98/spu_recomp.c, the copy in
function 73E0 itself: the lift also has a second copy of these blocks in function 75D8) and writes
gow_edge_73e0_body.h into the build tree:

  GOW_73_REGS()     u128 locals for every register the region mentions
  GOW_73_LOAD()     rN = g[N] for every register the region mentions (a register written on one path and
                    read on another is an input on the skip paths, so read-before-write in text order is
                    not enough; the copies are once per region entry)
  GOW_73_STORE()    g[N] = rN for every register the region writes
  GOW_73_S79B0()    the outer loop head, up to `if (r116 == 0) goto 7DC0`
  GOW_73_S79B0T()   the two statements from there to loc_00007BB4
  GOW_73_B7BB4()    inner loop 1, up to its back edge `if (r17 == 0) goto 7BB4`
  GOW_73_S7DC0()    from loc_00007DC0 up to `if (r116 == 0) goto 8058`
  GOW_73_S7DC0T()   from there to loc_00007ED8
  GOW_73_B7ED8()    inner loop 2, up to its back edge `if (r58 == 0) goto 7ED8`
  GOW_73_S8058()    from loc_00008058 up to the outer back edge `if (r52 != 0) goto 79B0`

The control flow between them is written out in gow_edge_native_73e0.c; this script checks that the lift has
exactly that shape (each branch line and label where expected, nothing else that branches) and fails
otherwise. Statements: ctx->gpr[N] -> rN, spu_ls_read128/write128(ctx, ...) -> LD/ST(...).
The port's CMakeLists.txt runs it whenever the lift changes. A lab lift works as well (a probe wrapper
renames 73E0 to _impl, --blocks adds counters at labels: both are handled).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, newline="").read().replace("\r\n", "\n").split("\n")
fn = next(i for i, l in enumerate(lines) if re.match(r"^void \w+_spu_func_000073E0(?:_impl)?\(spu_context\* ctx\) \{", l))
fn_end = next(i for i in range(fn + 1, len(lines)) if lines[i].startswith("}"))
seq = []   # (kind, text) from loc_000079B0 to loc_000081C0
inside = False
for l in lines[fn:fn_end]:
    s = l.strip()
    s = re.sub(r"g_fnp_blk\[\d+\]\+\+;", "", s).strip()   # --blocks probe counters
    m = re.match(r"^loc_([0-9A-F]{8}):\s*(.*)$", s)
    if m:
        if m.group(1) == "000079B0":
            inside = True
        elif m.group(1) == "000081C0" and inside:
            break
        if inside:
            seq.append(("label", m.group(1)))
        s = m.group(2).strip()
    if not inside or not s or s in ("/* branch hint (ignored) */;", "/* nop */;"):
        continue
    seq.append(("stmt", s))
else:
    sys.exit("73E0: loc_000079B0 .. loc_000081C0 not found")

# the expected shape: (segment name, statements until this terminator)
G = lambda r, op, lab: f"if (ctx->gpr[{r}]._u32[0] {op} 0) goto loc_{lab};"
shape = [("S79B0", ("stmt", G(116, "==", "00007DC0"))),
         ("S79B0T", ("label", "00007BB4")),
         ("B7BB4", ("stmt", G(17, "==", "00007BB4"))),
         ("*", ("label", "00007DC0")),
         ("S7DC0", ("stmt", G(116, "==", "00008058"))),
         ("S7DC0T", ("label", "00007ED8")),
         ("B7ED8", ("stmt", G(58, "==", "00007ED8"))),
         ("*", ("label", "00008058")),
         ("S8058", ("stmt", G(52, "!=", "000079B0")))]
assert seq[0] == ("label", "000079B0")
pos, segs = 1, {}
for name, term in shape:
    body = []
    while pos < len(seq) and seq[pos] != term:
        body.append(seq[pos]); pos += 1
    if pos == len(seq):
        sys.exit(f"73E0 region: {term} not found after segment {name}")
    pos += 1
    if name == "*":
        if body:
            sys.exit(f"73E0 region: unexpected statements before {term}: {body[:3]}")
        continue
    for k, s in body:
        if k == "label" or "goto" in s or "return" in s or "spu_func_" in s or "SPU_RET" in s or "pc =" in s:
            sys.exit(f"73E0 region, segment {name}: unexpected control flow: {k} {s}")
    segs[name] = [s for _, s in body]
if pos != len(seq):
    sys.exit(f"73E0 region: {len(seq) - pos} items after the outer back edge")

GPR = re.compile(r"ctx->gpr\[(\d+)\]")
ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (.*);$")
mentioned, written = set(), set()
out = {}
for name, body in segs.items():
    o = []
    for s in body:
        m = ASSIGN.match(s)
        if not m and not s.startswith("spu_ls_write128(ctx,"):
            sys.exit(f"unexpected statement in segment {name}: {s}")
        mentioned |= set(map(int, GPR.findall(s)))
        if m:
            written.add(int(m.group(1)))
        s = s.replace("spu_ls_read128(ctx, ", "LD(").replace("spu_ls_write128(ctx, ", "ST(")
        o.append(GPR.sub(lambda g: f"r{g.group(1)}", s))
    if "ctx" in " ".join(o):
        sys.exit(f"ctx left in segment {name}")
    out[name] = o
mentioned |= {17, 52, 58, 116}   # the branch registers

regs = sorted(mentioned)
w = max(len(s) for o in out.values() for s in o)
h = ["/* generated by gen_edge_73e0_body.py from the d935 lift -- game code, do not commit */",
     "#define GOW_73_REGS() u128 " + ", ".join(f"r{n}" for n in regs) + ";", "",
     "#define GOW_73_LOAD() \\"] + [f"    r{n} = g[{n}]; \\" for n in regs] + ["", "",
     "#define GOW_73_STORE() \\"] + [f"    g[{n}] = r{n}; \\" for n in sorted(written)] + ["", ""]
for name, o in out.items():
    h += [f"#define GOW_73_{name}() \\"] + [f"    {s.ljust(w)} \\" for s in o] + ["", ""]
open(dst, "w", newline="\n").write("\n".join(h))
print(f"73E0 region: " + ", ".join(f"{k} {len(v)}" for k, v in out.items()) +
      f" statements; {len(regs)} registers, {len(written)} written -> {dst}")
