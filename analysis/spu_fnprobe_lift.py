"""spu_fnprobe_lift.py <spu_recomp.c> <out.c> <fn_hex> [read_fn_hex ...]: a lab lift with a boundary probe.

Renames the lifted function <fn> to <name>_impl and adds a wrapper under the original name that calls
spu_fn_probe(ctx, fn, 0), the impl, then spu_fn_probe(ctx, fn, 1) (toolkit runtime/spu/spu_fn_probe.c). For
<fn> and every read_fn listed (its callees, so the read set is inclusive), spu_ls_read128(ctx, ...) becomes
spu_fn_probe_read(ctx, ...), which records the line while the probe window is open.

Lab builds only (work/buildlab.sh with LAB_LIFT): the wrapper and the routed reads cost time in any build.
Run with SPU_JOB_REPLAY=<captures> SPU_FN_PROBE=<fn> [SPU_FN_PROBE_SAVE=<dir>[,<n>]].
"""
import re
import sys

src, dst, fn = sys.argv[1], sys.argv[2], sys.argv[3].upper().rjust(8, "0")
reads = {a.upper().rjust(8, "0") for a in sys.argv[3:]}
text = open(src, newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
hdr = re.compile(r"^void (\w+_spu_func_([0-9A-F]{8}))\(spu_context\* ctx\) \{\s*$")
out, cur, name, done = [], None, None, 0
decl = ("u128 spu_fn_probe_read(const spu_context* ctx, uint32_t lsa);" + nl +
        "void spu_fn_probe(spu_context* ctx, uint32_t fn, int phase);")
for i, l in enumerate(lines):
    m = hdr.match(l)
    if m:
        if cur == fn:   # the impl ended just before this header: add the wrapper
            out += [f"void {name}(spu_context* ctx) {{",
                    f"    spu_fn_probe(ctx, 0x{int(fn, 16):X}u, 0); {name}_impl(ctx); spu_fn_probe(ctx, 0x{int(fn, 16):X}u, 1);",
                    "}", ""]
            done += 1
        cur = m.group(2)
        if cur == fn:
            name = m.group(1)
            out += [f"void {name}_impl(spu_context* ctx) {{"]
            continue
    elif cur in reads:
        l = l.replace("spu_ls_read128(ctx,", "spu_fn_probe_read(ctx,")
    out.append(l)
if cur == fn:
    out += [f"void {name}(spu_context* ctx) {{",
            f"    spu_fn_probe(ctx, 0x{int(fn, 16):X}u, 0); {name}_impl(ctx); spu_fn_probe(ctx, 0x{int(fn, 16):X}u, 1);",
            "}", ""]
    done += 1
if done != 1:
    sys.exit(f"function {fn} not found or found {done} times")
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out.insert(last_inc + 1, decl)   # callees come before the function in the file
open(dst, "w", newline="").write(nl.join(out))
print(f"wrapped {name}; reads routed in {len(reads)} function(s)")
