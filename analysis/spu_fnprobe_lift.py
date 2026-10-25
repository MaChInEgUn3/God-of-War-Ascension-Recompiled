"""spu_fnprobe_lift.py [--no-reads] [--blocks] <spu_recomp.c> <out.c> <fn_hex> [callee_hex ...]: a lab lift
with a boundary probe.

Renames the lifted function <fn> to <name>_impl and adds a wrapper under the original name that calls
spu_fn_probe(ctx, fn, 0), the impl, then spu_fn_probe(ctx, fn, 1) (toolkit runtime/spu/spu_fn_probe.c).
For <fn> and every callee listed:
- spu_ls_read128(ctx, ...) becomes spu_fn_probe_read(ctx, ...), which records the line while the probe window
  is open (skipped with --no-reads: those out-of-line calls inflate the per-call time);
- with --blocks, every `loc_<pc>:` label (and each function's entry) bumps a counter, and the wrapper hands
  the table to spu_fn_probe_blocks so each call reports its block counts ([fnpb]). Guest state is untouched.

Lab builds only (work/buildlab.sh with LAB_LIFT): the wrapper and the routed reads cost time in any build.
Run with SPU_JOB_REPLAY=<captures> SPU_FN_PROBE=<fn> [SPU_FN_PROBE_SAVE=<dir>[,<n>]].
"""
import re
import sys

args = sys.argv[1:]
no_reads = "--no-reads" in args
blocks = "--blocks" in args
args = [a for a in args if not a.startswith("--")]
src, dst, fn = args[0], args[1], args[2].upper().rjust(8, "0")
funcs = {a.upper().rjust(8, "0") for a in args[2:]}
text = open(src, newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
hdr = re.compile(r"^void (\w+_spu_func_([0-9A-F]{8}))\(spu_context\* ctx\) \{\s*$")
lab = re.compile(r"^(\s*)loc_([0-9A-F]{8}):(.*)$")
out, cur, name, done = [], None, None, 0
bpc = []   # block table: guest pc of each counted block


def count(pc):
    bpc.append(pc)
    return f"g_fnp_blk[{len(bpc) - 1}]++;"


def wrapper():
    f = f"0x{int(fn, 16):X}u"
    reg = "spu_fn_probe_blocks(g_fnp_blk, g_fnp_blk_pc, FNP_NBLK); " if blocks else ""
    return [f"void {name}(spu_context* ctx) {{",
            f"    {reg}spu_fn_probe(ctx, {f}, 0); {name}_impl(ctx); spu_fn_probe(ctx, {f}, 1);",
            "}", ""]


for l in lines:
    m = hdr.match(l)
    if m:
        if cur == fn:   # the impl ended just before this header
            out += wrapper(); done += 1
        cur = m.group(2)
        head = f"void {m.group(1)}_impl(spu_context* ctx) {{" if cur == fn else l
        if cur == fn:
            name = m.group(1)
        out.append(head)
        if blocks and cur in funcs:
            out.append("        " + count(int(cur, 16)))
        continue
    if cur in funcs:
        if not no_reads:
            l = l.replace("spu_ls_read128(ctx,", "spu_fn_probe_read(ctx,")
        lm = lab.match(l)
        if blocks and lm:
            l = f"{lm.group(1)}loc_{lm.group(2)}: {count(int(lm.group(2), 16))}{lm.group(3)}"
    out.append(l)
if cur == fn:
    out += wrapper(); done += 1
if done != 1:
    sys.exit(f"function {fn} not found or found {done} times")
decl = ["u128 spu_fn_probe_read(const spu_context* ctx, uint32_t lsa);",
        "void spu_fn_probe(spu_context* ctx, uint32_t fn, int phase);"]
if blocks:
    decl += ["void spu_fn_probe_blocks(uint32_t* cnt, const uint32_t* pc, unsigned n);",
             f"#define FNP_NBLK {len(bpc)}u",
             f"static uint32_t g_fnp_blk[{len(bpc)}];",
             "static const uint32_t g_fnp_blk_pc[] = { " + ", ".join(f"0x{p:X}" for p in bpc) + " };"]
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out[last_inc + 1:last_inc + 1] = decl   # callees come before the function in the file
open(dst, "w", newline="").write(nl.join(out))
print(f"wrapped {name}; {len(funcs)} function(s): reads {'kept' if no_reads else 'routed'}"
      f"{f', {len(bpc)} blocks counted' if blocks else ''}")
