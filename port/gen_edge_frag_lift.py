"""gen_edge_frag_lift.py <spu_recomp.c> <out.c> [<reference lift>]: an EDGE overlay fragment lift with its
native-switchable loop (port gow_edge_native_o11430.c), picked at run time by an env variable. The fragments
are separate lifts (spu/edgefrag_<blob>_<lsa>/spu_recomp.c, registered by spu/edgefrags_register.c), one per
LS placement <lsa> of an EBOOT code blob, so this is not gen_edge_native_lift.py (the d935 module's switches).

The loop is found by its offset in the blob, not by an LS address: BLOBS gives, per blob, the loop head's
offset and the shapes to check. One blob so far:

1. 7228c0 (input decode, A1 fixed point): the loop at blob+0x310 (in placement 11150: loc_00011460 in function
   11430, the loop the native kernel was written from), by GOW_EDGE_O11430 (g_edge_o11430, read once by
   gow_edge_o11430_mode): unset/0 the lifted loop (the default), 1 native packed v3 (GOW_EDGE_O11430_V2=1:
   v2), 2 native v1, 3/4 check. Every placement calls the same native function with its live registers (the
   loop has no LS address of its own: gen_edge_o11430_body.py checks that on 11150's copy, and this script
   checks that each placement's loop is that copy, statement for statement).

Head H = lsa + offset. The placements' lifts split the code at different points, so the loop comes in two
shapes; the switch always sits on the edge that enters the loop from the prologue, never on the back edge:

  A. label `loc_<H>:` with the back edge `if (r19 != 0) goto loc_<H>;` in the same function (11150 and 7
     more). The switch goes before the label (reached only by falling into it; the back edge jumps past it):
         if (g_edge_o11430) { if (g_edge_o11430 <= 2) { gow_edge_o11430_11460(ctx); goto gow_<H>_exit; }
                              gow_edge_o11430_check_begin_at(ctx, 0x<lsa>u); }
     and after the back edge the label `gow_<H>_exit: if (g_edge_o11430 > 2) gow_edge_o11430_check_end(ctx);`.
     If the label is in more than one function, the one that starts before H (the prologue falls into the
     loop there) gets the switch; a function that starts at H is an alternate entry and keeps its lifted copy
     (11150's function 11460), as before.
  B. the head is function <H> (no label), the loop runs through a chain of functions linked by
     `{ SPU_DIRECT(ctx, next, fn); }` and the back edge `if (r19 != 0) { SPU_DIRECT(ctx, 0x<H>, fn_<H>); }`
     is in a later function. The switch goes before the one unconditional `{ SPU_DIRECT(ctx, 0x<H>, ..); }`
     (the prologue's fall-through into the loop); the native path then runs the loop's exit tail, copied from
     after the back edge (`r3 = il(0); r1 = ai(r1, 224); SPU_RET`), and check_end goes after the back edge.
     Other entries into function <H> (and the back edge) stay lifted.

Each placement's loop statements (head to back edge, function hops, labels and nops dropped) must equal the
reference lift's (default: the 11150 lift beside this one); each site must be found exactly once, else this
fails. The port's CMakeLists.txt builds every placement lift of the blob through this into the build tree
(<build>/edgefrag_<blob>_<lsa>_native.c; the original drops out of the sources and is never written). The copy
includes the lift's own spu_recomp.h by its absolute path, since it no longer sits beside it. A lift that
already has the switch is copied unchanged apart from that include (work/buildlab.sh LAB_LIFT).
"""
import os
import re
import sys

BLOBS = {
    # blob EA: loop head offset, reference placement, first loop statement, exit tail, back-edge condition
    "7228c0": dict(off=0x310, ref="11150",
                   first="ctx->gpr[78] = spu_and(ctx->gpr[9], ctx->gpr[33]);",
                   tail=["ctx->gpr[3] = spu_il(0);", "ctx->gpr[1] = spu_ai(ctx->gpr[1], 224);", "SPU_RET(ctx);"],
                   cond="if (ctx->gpr[19]._u32[0] != 0)"),
}

src, dst = sys.argv[1], sys.argv[2]
text = open(src, newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
inc = '#include "spu_recomp.h"'
hdr_path = os.path.join(os.path.dirname(os.path.abspath(src)), "spu_recomp.h").replace("\\", "/")
incs = [i for i, l in enumerate(lines) if l == inc or re.match(r'^#include ".*/spu_recomp\.h"$', l)]
if len(incs) != 1:
    sys.exit(f"{src}: want exactly one line {inc}")
lines[incs[0]] = f'#include "{hdr_path}"'
m = re.search(r"^void edgefrag_([0-9a-f]+)_([0-9a-f]+)_spu_recomp_register\(void\)", text, re.M)
if not m or m.group(1) not in BLOBS:
    sys.exit(f"{src}: not a placement lift of a known blob ({', '.join(BLOBS)})")
blob, lsa = m.group(1), int(m.group(2), 16)
B = BLOBS[blob]
H = lsa + B["off"]
h = f"{H:08X}"
if "gow_edge_o11430_11460(" in text:
    open(dst, "w", newline="").write(nl.join(lines))
    sys.exit(print(f"{dst}: the switch is already there; copied"))

HDR = re.compile(r"^void (\w+_spu_func_([0-9A-F]{8}))(_impl)?\(spu_context\* ctx\) \{\s*$")
HOP = re.compile(r"^\{ SPU_DIRECT\(ctx, 0x([0-9A-F]+), (\w+)\); \}$")


def functions(ls):
    """[(header line, closing line, name, address)] of the lifted functions."""
    fns = []
    for i, l in enumerate(ls):
        mm = HDR.match(l)
        if mm:
            j = i + 1
            while ls[j] != "}":
                j += 1
            fns.append((i, j, mm.group(1) + (mm.group(3) or ""), int(mm.group(2), 16)))
    return fns


def loop_body(ls, fns, head, where):
    """The loop's statements from the head to the back edge, and the back edge's line index and shape."""
    hx = f"{head:08X}"
    labels = [(i, f) for f in fns for i in range(f[0] + 1, f[1]) if re.match(rf"^\s*loc_{hx}:", ls[i])]
    if labels:   # shape A
        inner = [x for x in labels if x[1][3] < head]
        pick = inner if inner else [x for x in labels if x[1][3] == head]
        if len(pick) != 1 or any(x[1][3] > head for x in labels):
            sys.exit(f"{where}: loop head loc_{hx}: {len(labels)} labels, none or several primary sites")
        i, f = pick[0]
        start, back_line = i + 1, f"{B['cond']} goto loc_{hx};"
    else:        # shape B
        heads = [f for f in fns if f[3] == head]
        if len(heads) != 1:
            sys.exit(f"{where}: no label loc_{hx} and {len(heads)} functions at 0x{head:X}")
        f = heads[0]
        start, back_line = f[0] + 1, f"{B['cond']} {{ SPU_DIRECT(ctx, 0x{head:X}, {f[2]}); }}"
    body, i = [], start
    while True:
        s = ls[i].strip()
        if s == back_line:
            return body, i, f
        if s == "}":
            sys.exit(f"{where}: the loop runs into the end of a function at line {i + 1} without a hop")
        hm = HOP.match(s)
        if hm:   # fall-through hop into the next function of the loop (shape B, or a split inside the loop)
            nxt = [g for g in fns if g[3] == int(hm.group(1), 16) and g[2] == hm.group(2)]
            if len(nxt) != 1 or nxt[0][3] <= f[3] or ls[i + 1] != "}":
                sys.exit(f"{where}: unexpected transfer in the loop at line {i + 1}: {s}")
            f = nxt[0]
            i = f[0] + 1
            continue
        if s and s != "/* nop */;" and not re.match(r"^loc_[0-9A-F]{8}:$", s) and "gow_edge_o11430" not in s \
                and not s.startswith("gow_") and "if (__builtin_expect(g_edge_o11430" not in s:
            if re.search(r"\bgoto\b|SPU_|\bif \(|\breturn\b|\bloc_", s):
                sys.exit(f"{where}: unexpected control flow in the loop at line {i + 1}: {s}")
            body.append(s)
        i += 1


fns = functions(lines)
body, back, bf = loop_body(lines, fns, H, src)
if not body or body[0] != B["first"]:
    sys.exit(f"{src}: the loop at 0x{H:X} does not start with {B['first']}")
ref = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(src))),
                                                         f"edgefrag_{blob}_{B['ref']}", "spu_recomp.c")
rlines = open(ref, newline="").read().replace("\r\n", "\n").split("\n")
rbody, _, _ = loop_body(rlines, functions(rlines), int(B["ref"], 16) + B["off"], ref)
if body != rbody:
    k = next((k for k, (a, b) in enumerate(zip(body, rbody)) if a != b), min(len(body), len(rbody)))
    sys.exit(f"{src}: the loop at 0x{H:X} differs from {B['ref']}'s ({len(body)} vs {len(rbody)} statements; "
             f"first at {k}: {body[k] if k < len(body) else '-'} | {rbody[k] if k < len(rbody) else '-'})")

mode = ["        if (__builtin_expect(g_edge_o11430 < 0, 0)) g_edge_o11430 = gow_edge_o11430_mode();"]
check_end = "        if (g_edge_o11430 > 2) gow_edge_o11430_check_end(ctx);"
begin = f" gow_edge_o11430_check_begin_at(ctx, 0x{lsa:X}u); }}"
ins = {}   # line index -> lines to put before it
after = {}  # line index -> lines to put after it
if lines[back].strip().endswith(f"goto loc_{h};"):   # shape A
    lab = [i for i in range(bf[0] + 1, back) if re.match(rf"^\s*loc_{h}:", lines[i])]
    if len(lab) != 1:
        sys.exit(f"{src}: loc_{h}: {len(lab)} in the back edge's function")
    ins[lab[0]] = mode + ["        if (g_edge_o11430) { if (g_edge_o11430 <= 2) { gow_edge_o11430_11460(ctx);"
                          f" goto gow_{h[3:]}_exit; }}" + begin]
    after[back] = [f"gow_{h[3:]}_exit:", check_end]
    shape = "A"
else:                                                 # shape B
    tail, i = [], back + 1
    while lines[i] != "}":
        if lines[i].strip():
            tail.append(lines[i].strip())
        i += 1
    if tail != B["tail"]:
        sys.exit(f"{src}: unexpected exit tail after the back edge: {tail}")
    hf = [f for f in fns if f[3] == H][0]
    entry = [i for i, l in enumerate(lines) if l.strip() == f"{{ SPU_DIRECT(ctx, 0x{H:X}, {hf[2]}); }}"]
    if len(entry) != 1:
        sys.exit(f"{src}: {len(entry)} fall-through entries into 0x{H:X} (want 1)")
    ins[entry[0]] = mode + ["        if (g_edge_o11430) { if (g_edge_o11430 <= 2) { gow_edge_o11430_11460(ctx); "
                            + " ".join(tail) + " }" + begin]
    after[back] = [check_end]
    shape = "B"
out = []
for i, l in enumerate(lines):
    out += ins.get(i, [])
    out.append(l)
    out += after.get(i, [])
decl = ["int gow_edge_o11430_mode(void);",
        "void gow_edge_o11430_11460(spu_context* ctx);",
        "void gow_edge_o11430_check_begin_at(spu_context* ctx, unsigned site);",
        "void gow_edge_o11430_check_end(spu_context* ctx);",
        "static int g_edge_o11430 = -1;"]
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out[last_inc + 1:last_inc + 1] = decl
open(dst, "w", newline="").write(nl.join(out))
print(f"{dst}: {blob} loop at 0x{H:X} (blob+0x{B['off']:X}, shape {shape}, {len(body)} statements) switchable by "
      f"GOW_EDGE_O11430")
