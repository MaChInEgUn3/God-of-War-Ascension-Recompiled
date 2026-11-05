"""gen_edge_frag_lift.py <spu_recomp.c> <out.c> [<reference lift>]: an EDGE overlay fragment lift with its
native-switchable loop, picked at run time by an env variable. The fragments are separate lifts
(spu/edgefrag_<blob>_<lsa>/spu_recomp.c, registered by spu/edgefrags_register.c), one per LS placement <lsa>
of an EBOOT code blob, so this is not gen_edge_native_lift.py (the d935 module's switches).
gen_edge_frag_lift.py --body <reference lift> <out.h>: the loop's macros for the blob's native file (only for
a blob whose BLOBS entry has `body`; 7228c0's come from gen_edge_o11430_body.py).

The loop is found by its offset in the blob, not by an LS address: BLOBS gives, per blob, the loop head's
offset and the shapes to check. Two blobs so far:

1. 7228c0 (input decode, A1 fixed point): the loop at blob+0x310 (in placement 11150: loc_00011460 in function
   11430, the loop the native kernel was written from), by GOW_EDGE_O11430 (g_edge_o11430, read once by
   gow_edge_o11430_mode): unset/0 the lifted loop (the default), 1 native packed v3 (GOW_EDGE_O11430_V2=1:
   v2), 2 native v1, 3/4 check. Every placement calls the same native function with its live registers (the
   loop has no LS address of its own: gen_edge_o11430_body.py checks that on 11150's copy, and this script
   checks that each placement's loop is that copy, statement for statement).
2. 719900 (output packing, family B): the loop at blob+0x600 (placement 11340: function 11940 and the four
   functions it chains to, back edge on r2), by GOW_EDGE_O719900 (port gow_edge_native_o719900.c; 0 lifted,
   1 native, 3 check). Its exit tail (the stack restores, SPU_RET) must equal the reference placement's.
   `--body` writes GOW_O719_REGS/LOAD/STORE/BODY from the reference's loop (ctx->gpr[N] -> rN, LS access
   -> LD/ST): the loop has no LS address of its own (no load or store at a constant address: checked).
3. 71fa90, 71c310 (input decode) and 723300 (output packing with an 11/11/10 field: the loop at blob+0x2B8,
   placement 11340: function 115F8 and the one it chains to, back edge on r9; GOW_EDGE_O723300, port
   gow_edge_native_o723300.c) the same way as 719900, each from its BLOBS entry.
4. 7298f0 (output side, float4 -> packed float3: the loop at blob+0xD0, placement 11530: function 11600, back
   edge on r43; GOW_EDGE_O7298F0, port gow_edge_native_o7298f0.c) and 728a90 (input side, packed float3 ->
   float4 with a constant w: the loop at blob+0x154, placement 11a20: function 11B74, back edge on r64;
   GOW_EDGE_O728A90, port gow_edge_native_o728a90.c), the family-C copies, the same way.

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
    # blob EA: loop head offset, reference placement, first loop statement, exit tail (None: the reference's),
    # back-edge condition, switch name (g_edge_<sw>, gow_edge_<sw>_*), native entry, env variable
    "7228c0": dict(off=0x310, ref="11150",
                   first="ctx->gpr[78] = spu_and(ctx->gpr[9], ctx->gpr[33]);",
                   tail=["ctx->gpr[3] = spu_il(0);", "ctx->gpr[1] = spu_ai(ctx->gpr[1], 224);", "SPU_RET(ctx);"],
                   cond="if (ctx->gpr[19]._u32[0] != 0)", sw="o11430", native="gow_edge_o11430_11460",
                   env="GOW_EDGE_O11430"),
    "719900": dict(off=0x600, ref="11340",
                   first="ctx->gpr[19] = spu_a(ctx->gpr[103], ctx->gpr[41]);",
                   tail=None, cond="if (ctx->gpr[2]._u32[0] != 0)", sw="o719900", native="gow_edge_o719900_loop",
                   env="GOW_EDGE_O719900", body="GOW_O719",
                   consts=[60, 63, 64, 67, 68, 69, 70, 71, 85, 95, 96, 97, 98, 99, 101, 104, 105, 107, 108, 109,
                           110, 111, 112, 113, 114, 115, 116, 117],
                   fma=[84, 94, 106, 100, 93]),
    "71fa90": dict(off=0x2E8, ref="12210",
                   first="ctx->gpr[18] = spu_ori(ctx->gpr[7], 0);",
                   tail=None, cond="if (ctx->gpr[71]._u32[0] != 0)", sw="o71fa90", native="gow_edge_o71fa90_loop",
                   env="GOW_EDGE_O71FA90", body="GOW_O71F",
                   consts=[30, 31, 32, 41, 42, 43, 52, 60, 62, 63, 64, 65, 66, 67, 68, 69, 70],
                   fma=[55, 54, 56], stack=(0x20, 0xE0)),
    "71c310": dict(off=0x4A0, ref="11db0",
                   first="ctx->gpr[25] = spu_and(ctx->gpr[3], ctx->gpr[68]);",
                   tail=None, cond="if (ctx->gpr[32]._u32[0] != 0)", sw="o71c310", native="gow_edge_o71c310_loop",
                   env="GOW_EDGE_O71C310", body="GOW_O71C"),
    "723300": dict(off=0x2B8, ref="11340",
                   first="ctx->gpr[5] = spu_fma(ctx->gpr[62], ctx->gpr[39], ctx->gpr[61]);",
                   tail=None, cond="if (ctx->gpr[9]._u32[0] != 0)", sw="o723300", native="gow_edge_o723300_loop",
                   env="GOW_EDGE_O723300", body="GOW_O723",
                   consts=[44, 54, 55, 56, 57, 58, 59, 65, 66, 67, 68, 69, 70, 71, 72, 73],
                   fma=[62, 61, 51, 60, 26]),
    "7298f0": dict(off=0xD0, ref="11530",
                   first="ctx->gpr[44] = spu_a(ctx->gpr[23], ctx->gpr[14]);",
                   tail=None, cond="if (ctx->gpr[43]._u32[0] != 0)", sw="o7298f0", native="gow_edge_o7298f0_loop",
                   env="GOW_EDGE_O7298F0", body="GOW_O729", consts=[8, 18, 24, 25, 26]),
    "728a90": dict(off=0x154, ref="11a20",
                   first="ctx->gpr[65] = spu_ori(ctx->gpr[15], 0);",
                   tail=None, cond="if (ctx->gpr[64]._u32[0] != 0)", sw="o728a90", native="gow_edge_o728a90_loop",
                   env="GOW_EDGE_O728A90", body="GOW_O728", consts=[17, 18, 19, 20, 21]),
}
SKIP = ("/* nop */;", "/* branch hint (ignored) */;")

body_mode = len(sys.argv) > 1 and sys.argv[1] == "--body"
if body_mode:
    sys.argv.pop(1)
consts_mode = len(sys.argv) > 1 and sys.argv[1] == "--consts"
if consts_mode:
    sys.argv.pop(1)
    eboot = sys.argv.pop(2)
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
SW, NATIVE = B["sw"], B["native"]
H = lsa + B["off"]
h = f"{H:08X}"
if not body_mode and not consts_mode and f"{NATIVE}(" in text:
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
        if s and s not in SKIP and not re.match(r"^loc_[0-9A-F]{8}:$", s) and f"gow_edge_{SW}" not in s \
                and not s.startswith("gow_") and f"if (__builtin_expect(g_edge_{SW}" not in s:
            if re.search(r"\bgoto\b|SPU_|\bif \(|\breturn\b|\bloc_", s):
                sys.exit(f"{where}: unexpected control flow in the loop at line {i + 1}: {s}")
            body.append(s)
        i += 1


def exit_tail(ls, back, fs=None):
    """The statements after the back edge up to the end of its function (shape B's exit). With the lift's
    functions `fs`, a tail split by the lifter continues through its fall-through hops (dropped from the list)
    into later functions, up to the function that ends without one."""
    tail, i = [], back + 1
    cur = max((f for f in fs if f[0] < back), key=lambda f: f[0]) if fs else None
    while True:
        while ls[i] != "}":
            if ls[i].strip():
                tail.append(ls[i].strip())
            i += 1
        hm = HOP.match(tail[-1]) if fs and tail else None
        if not hm:
            return tail
        nxt = [g for g in fs if g[3] == int(hm.group(1), 16) and g[2] == hm.group(2)]
        if len(nxt) != 1 or nxt[0][3] <= cur[3]:
            return tail   # not a fall-through hop: left in the tail, the comparison fails
        tail.pop()
        cur = nxt[0]
        i = cur[0] + 1


fns = functions(lines)
body, back, bf = loop_body(lines, fns, H, src)
if not body or body[0] != B["first"]:
    sys.exit(f"{src}: the loop at 0x{H:X} does not start with {B['first']}")

if consts_mode:   # the loop's controls and masks at its entry, for the native file's entry check
    if "consts" not in B:
        sys.exit(f"{src}: blob {blob} has no --consts registers")
    import struct
    elf = open(eboot, "rb").read()
    phoff, phent, phnum = struct.unpack(">Q", elf[0x20:0x28])[0], *struct.unpack(">HH", elf[0x36:0x3A])

    def blob_quad(lsa_addr):   # the blob's bytes at that LS address of this placement, from the EBOOT
        va = int(blob, 16) + lsa_addr - lsa
        for i in range(phnum):
            _, _, off, vaddr, _, filesz = struct.unpack(">IIQQQQ", elf[phoff + i * phent:phoff + i * phent + 40])
            if vaddr <= va and va + 16 <= vaddr + filesz:
                return list(elf[off + va - vaddr:off + va - vaddr + 16])
        sys.exit(f"{eboot}: blob byte 0x{va:X} not in a loadable segment")

    # Run the prologue (the placement's entry function and the functions it falls through to, up to the loop
    # head) on constants only: every register starts unknown, a value is known only if it comes from
    # immediates and blob bytes through the ops below; the controls must all come out known.
    W = lambda q: [int.from_bytes(bytes(q[4 * i:4 * i + 4]), "big") for i in range(4)]
    Q = lambda ws: [b for w in ws for b in (w & 0xFFFFFFFF).to_bytes(4, "big")]
    sx = lambda v, bits: v - (1 << bits) if v & (1 << (bits - 1)) else v
    OPS = {
        "il": lambda i: Q([sx(i & 0xFFFF, 16)] * 4), "ilh": lambda i: Q([((i & 0xFFFF) << 16) | (i & 0xFFFF)] * 4),
        "ilhu": lambda i: Q([(i & 0xFFFF) << 16] * 4), "iohl": lambda a, i: Q([w | (i & 0xFFFF) for w in W(a)]),
        "fsmbi": lambda i: [0xFF if (i >> (15 - k)) & 1 else 0 for k in range(16)],
        "xorbi": lambda a, i: [b ^ (i & 0xFF) for b in a], "orbi": lambda a, i: [b | (i & 0xFF) for b in a],
        "andbi": lambda a, i: [b & i & 0xFF for b in a],
        "ori": lambda a, i: Q([w | sx(i & 0x3FF, 10) for w in W(a)]),
        "ai": lambda a, i: Q([w + sx(i & 0x3FF, 10) for w in W(a)]),
        "a": lambda a, b: Q([x + y for x, y in zip(W(a), W(b))]), "sf": lambda a, b: Q([y - x for x, y in zip(W(a), W(b))]),
        "and": lambda a, b: [x & y for x, y in zip(a, b)], "andc": lambda a, b: [x & ~y & 0xFF for x, y in zip(a, b)],
        "selb": lambda a, b, m: [(x & ~k | y & k) & 0xFF for x, y, k in zip(a, b, m)],
        "shlqbyi": lambda a, n: [a[k + (n & 31)] if k + (n & 31) < 16 else 0 for k in range(16)],
        "rotqbyi": lambda a, n: [a[(k + n) & 15] for k in range(16)],
        "rotqmbyi": lambda a, n: [a[k - ((-n) & 31)] if k - ((-n) & 31) >= 0 else 0 for k in range(16)],
        "shufb": lambda a, b, c: [(0 if x < 0xC0 else 0xFF if x < 0xE0 else 0x80) if x & 0x80 else (a + b)[x & 31]
                                  for x in c],
    }
    regs, stack = {}, {}   # stack: the quads the prologue stores at r1 + a positive offset (a table the loop reads)
    ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = spu_(\w+)\((.*)\);$")
    SST = re.compile(r"^spu_ls_write128\(ctx, ctx->gpr\[(\d+)\]\._u32\[0\] \+ (0x[0-9A-F]+), ctx->gpr\[(\d+)\]\);$")
    f = next((f for f in fns if f[3] == lsa), None)
    while f is not None and f[3] != H:
        nxt = None
        for s in (x.strip() for x in lines[f[0] + 1:f[1]]):
            if s in SKIP or s.startswith("spu_ls_write128(ctx, ctx->gpr[1]._u32[0] + -0x"):
                continue
            hm = HOP.match(s)
            if hm:
                nxt = [g for g in fns if g[3] == int(hm.group(1), 16) and g[2] == hm.group(2)]
                break
            st = SST.match(s)
            if st:   # a store: kept if it is to the stack frame (the table), else not followed (ctx fields)
                if st.group(1) == "1" and "stack" in B:
                    stack[int(st.group(2), 16)] = regs.get(int(st.group(3)))
                continue
            a = ASSIGN.match(s)
            if not a:
                sys.exit(f"{src}: unexpected statement in the prologue: {s}")
            d, op, args = int(a.group(1)), a.group(2), [x.strip() for x in a.group(3).split(",")]
            vals = []
            for x in args:
                g = re.fullmatch(r"ctx->gpr\[(\d+)\]", x)
                vals.append(regs.get(int(g.group(1))) if g else (int(x, 0) if re.fullmatch(r"-?(0x[0-9A-Fa-f]+|\d+)", x) else None))
            if op == "ls_read128" and len(args) == 2 and isinstance(vals[1], int):
                regs[d] = blob_quad(vals[1])
            elif op in OPS and None not in vals:
                regs[d] = OPS[op](*vals)
            else:
                regs.pop(d, None)
        if not nxt or len(nxt) != 1:
            sys.exit(f"{src}: the prologue does not fall through to the loop head 0x{H:X}")
        f = nxt[0]
    if f is None:
        sys.exit(f"{src}: no function at the placement's entry 0x{lsa:X}")
    miss = [r for r in B["consts"] + B.get("fma", []) if r not in regs]
    if miss:
        sys.exit(f"{src}: the prologue does not set r{miss} from constants alone")
    P = B["body"]
    o = [f"/* generated by gen_edge_frag_lift.py --consts from {os.path.basename(os.path.dirname(os.path.abspath(src)))}"
         f" and the blob's bytes in the EBOOT: the {len(B['consts'])} controls and masks the loop at blob {blob} + "
         f"0x{B['off']:X} gets from its prologue (SPU words). Game data: not committed. */",
         f"#define {P}_NCTL {len(B['consts'])}",
         f"#define {P}_CTL {{ \\"]
    o += [f"    {{ {r}, {{ " + ", ".join(f"0x{w:08X}u" for w in W(regs[r])) + " } }, \\" for r in B["consts"]]
    o += ["}"]
    if B.get("fma"):   # the fma operands and shift counts the prologue sets (the native file's self-test uses them)
        o += [f"#define {P}_FMA {{ \\"]
        o += [f"    {{ {r}, {{ " + ", ".join(f"0x{w:08X}u" for w in W(regs[r])) + " } }, \\" for r in B["fma"]]
        o += ["}"]
    if B.get("stack"):   # the table the prologue writes into its stack frame, r1 + lo .. hi (16-byte entries)
        lo, hi = B["stack"]
        bad = [f"0x{x:X}" for x in range(lo, hi, 16) if stack.get(x) is None]
        if bad:
            sys.exit(f"{src}: the prologue does not store a constant at r1 + {bad}")
        o += [f"#define {P}_NTAB {(hi - lo) // 16}", f"#define {P}_TAB0 0x{lo:X}", f"#define {P}_TAB {{ \\"]
        o += ["    { " + ", ".join(f"0x{w:08X}u" for w in W(stack[x])) + " }, \\" for x in range(lo, hi, 16)]
        o += ["}"]
    o += [""]
    open(dst, "w", newline="\n").write("\n".join(o))
    sys.exit(print(f"{dst}: {blob}: {len(B['consts'])} controls and {len(B.get('fma', []))} fma operands from the prologue"))

if body_mode:   # the loop macros for the native file, from this (the reference) lift
    if "body" not in B:
        sys.exit(f"{src}: blob {blob} has no --body macros (see its own body generator)")
    P = B["body"]
    GPR = re.compile(r"ctx->gpr\[(\d+)\]")
    ASSIGN = re.compile(r"^ctx->gpr\[(\d+)\] = (spu_\w+)\((.*)\);$")
    STORE = re.compile(r"^spu_ls_write128\(ctx, (.*), ctx->gpr\[(\d+)\]\);$")
    regs, written, out_body = set(), [], []
    for s in body:
        regs.update(int(x) for x in GPR.findall(s))
        a, st = ASSIGN.match(s), STORE.match(s)
        if not a and not st:
            sys.exit(f"{src}: unexpected statement in the loop: {s}")
        if a:
            if int(a.group(1)) not in written:
                written.append(int(a.group(1)))
            if a.group(2) == "spu_ls_read128" and not GPR.search(a.group(3)):
                sys.exit(f"{src}: a load at a constant LS address in the loop: {s}")
        if st and not GPR.search(st.group(1)):
            sys.exit(f"{src}: a store at a constant LS address in the loop: {s}")
        t = s.replace("spu_ls_read128(ctx, ", "LD(").replace("spu_ls_write128(ctx, ", "ST(")
        if "ctx" in GPR.sub("", t):
            sys.exit(f"{src}: a statement uses ctx otherwise than through its registers: {s}")
        out_body.append(GPR.sub(lambda mm: f"r{mm.group(1)}", t))
    rs = sorted(regs)
    o = [f"/* generated by gen_edge_frag_lift.py --body from {os.path.basename(os.path.dirname(os.path.abspath(src)))}"
         f": the loop at blob {blob} + 0x{B['off']:X} (head 0x{H:X}), {len(body)} statements, back edge "
         f"{B['cond']}. Game code: not committed. */",
         f"#define {P}_NSTMT {len(body)}",
         f"#define {P}_REGS() u128 " + ", ".join(f"r{r}" for r in rs) + ";",
         f"#define {P}_LOAD() " + " ".join(f"r{r} = g[{r}];" for r in rs),
         f"#define {P}_STORE() " + " ".join(f"g[{r}] = r{r};" for r in sorted(written)),
         f"#define {P}_BODY() \\"]
    o += [f"    {s} \\" for s in out_body] + ["    /* end */", ""]
    open(dst, "w", newline="\n").write("\n".join(o))
    sys.exit(print(f"{dst}: {blob} loop at 0x{H:X}: {len(body)} statements, {len(rs)} registers, "
                   f"{len(written)} written"))

ref = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(src))),
                                                         f"edgefrag_{blob}_{B['ref']}", "spu_recomp.c")
rlines = open(ref, newline="").read().replace("\r\n", "\n").split("\n")
rfns = functions(rlines)
rbody, rback, _ = loop_body(rlines, rfns, int(B["ref"], 16) + B["off"], ref)
if B["tail"] is None:   # the exit tail is the reference's (it has no LS address either)
    B["tail"] = exit_tail(rlines, rback, rfns)
    if not B["tail"] or B["tail"][-1] != "SPU_RET(ctx);" or any(re.search(r"\bgoto\b|SPU_DIRECT|\bif \(|0x1[0-9A-F]{4}\b", t)
                                                               for t in B["tail"]):
        sys.exit(f"{ref}: the reference's exit tail is not straight code ending in SPU_RET: {B['tail']}")
if body != rbody:
    k = next((k for k, (a, b) in enumerate(zip(body, rbody)) if a != b), min(len(body), len(rbody)))
    sys.exit(f"{src}: the loop at 0x{H:X} differs from {B['ref']}'s ({len(body)} vs {len(rbody)} statements; "
             f"first at {k}: {body[k] if k < len(body) else '-'} | {rbody[k] if k < len(rbody) else '-'})")

G = f"g_edge_{SW}"
mode = [f"        if (__builtin_expect({G} < 0, 0)) {G} = gow_edge_{SW}_mode();"]
check_end = f"        if ({G} > 2) gow_edge_{SW}_check_end(ctx);"
begin = f" gow_edge_{SW}_check_begin_at(ctx, 0x{lsa:X}u); }}"
ins = {}   # line index -> lines to put before it
after = {}  # line index -> lines to put after it
if lines[back].strip().endswith(f"goto loc_{h};"):   # shape A
    lab = [i for i in range(bf[0] + 1, back) if re.match(rf"^\s*loc_{h}:", lines[i])]
    if len(lab) != 1:
        sys.exit(f"{src}: loc_{h}: {len(lab)} in the back edge's function")
    ins[lab[0]] = mode + [f"        if ({G}) {{ if ({G} <= 2) {{ {NATIVE}(ctx);"
                          f" goto gow_{h[3:]}_exit; }}" + begin]
    after[back] = [f"gow_{h[3:]}_exit:", check_end]
    shape = "A"
else:                                                 # shape B
    tail = exit_tail(lines, back, fns if blob != "7228c0" else None)
    if tail != B["tail"]:
        sys.exit(f"{src}: unexpected exit tail after the back edge: {tail}")
    hf = [f for f in fns if f[3] == H][0]
    entry = [i for i, l in enumerate(lines) if l.strip() == f"{{ SPU_DIRECT(ctx, 0x{H:X}, {hf[2]}); }}"]
    if len(entry) != 1:
        sys.exit(f"{src}: {len(entry)} fall-through entries into 0x{H:X} (want 1)")
    run = [t for t in tail if t not in SKIP]
    if len(run) <= 3:
        ins[entry[0]] = mode + [f"        if ({G}) {{ if ({G} <= 2) {{ {NATIVE}(ctx); " + " ".join(run) + " }" + begin]
    else:
        ins[entry[0]] = mode + [f"        if ({G}) {{ if ({G} <= 2) {{ {NATIVE}(ctx);"] \
            + [f"                {t}" for t in run] + ["            }" + begin]
    after[back] = [check_end]
    shape = "B"
out = []
for i, l in enumerate(lines):
    out += ins.get(i, [])
    out.append(l)
    out += after.get(i, [])
decl = [f"int gow_edge_{SW}_mode(void);",
        f"void {NATIVE}(spu_context* ctx);",
        f"void gow_edge_{SW}_check_begin_at(spu_context* ctx, unsigned site);",
        f"void gow_edge_{SW}_check_end(spu_context* ctx);",
        f"static int {G} = -1;"]
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out[last_inc + 1:last_inc + 1] = decl
open(dst, "w", newline="").write(nl.join(out))
print(f"{dst}: {blob} loop at 0x{H:X} (blob+0x{B['off']:X}, shape {shape}, {len(body)} statements) switchable by "
      f"{B['env']}")
