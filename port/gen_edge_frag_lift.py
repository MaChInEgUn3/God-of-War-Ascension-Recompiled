"""gen_edge_frag_lift.py <spu_recomp.c> <out.c>: an EDGE overlay fragment lift with its native-switchable region
(port gow_edge_native_o11430.c), picked at run time by an env variable. The fragments are separate lifts
(spu/edgefrag_<overlay>_<entry>/spu_recomp.c, registered by spu/edgefrags_register.c), so this is not
gen_edge_native_lift.py (the d935 module's switches); it knows one fragment so far:

1. edgefrag_7228c0_11150 (overlay 0x7228C0, entry 0x11150): function 11430's loop at loc_00011460 (exit:
   the fall-through after its back edge, then 11430's return), by GOW_EDGE_O11430 (g_edge_o11430, read
   once by gow_edge_o11430_mode). The loop is entered only by falling through from 11430's entry block;
   its back edge jumps to the label. In edgefrag_7228c0_11150_spu_func_00011430 (or its _impl, so it
   composes with spu_fnprobe_lift.py in either order):
         if (g_edge_o11430) { if (g_edge_o11430 <= 2) { gow_edge_o11430_11460(ctx); goto gow_11460_exit; }
                              gow_edge_o11430_check_begin(ctx); }
   before `loc_00011460:` and, after the back edge `if (ctx->gpr[19]._u32[0] != 0) goto loc_00011460;`,
   the label `gow_11460_exit: if (g_edge_o11430 > 2) gow_edge_o11430_check_end(ctx);`. Only function 11430:
   the alternate-entry function 11460 keeps its lifted copy, and so do the other fragment lifts that have
   a function at 0x11430 (other overlays, other code). g_edge_o11430: unset/0 the lifted loop (the default),
   1 native packed v3 (GOW_EDGE_O11430_V2=1: v2), 2 native v1, 3/4 check.

The port's CMakeLists.txt builds that fragment's lift through this into the build tree
(<build>/edgefrag_7228c0_11150_native.c; the original drops out of the sources and is never written). The
copy includes the lift's own spu_recomp.h by its absolute path, since it no longer sits beside it. A lift
that already has the switch is copied unchanged apart from that include (work/buildlab.sh LAB_LIFT).
"""
import os
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
FN = "edgefrag_7228c0_11150_spu_func_00011430"
BACK = "if (ctx->gpr[19]._u32[0] != 0) goto loc_00011460;"
inc = '#include "spu_recomp.h"'
hdr_path = os.path.join(os.path.dirname(os.path.abspath(src)), "spu_recomp.h").replace("\\", "/")
incs = [i for i, l in enumerate(lines) if l == inc or re.match(r'^#include ".*/spu_recomp\.h"$', l)]
if len(incs) != 1:
    sys.exit(f"{src}: want exactly one line {inc}")
lines[incs[0]] = f'#include "{hdr_path}"'
if f"void {FN}" not in text:
    sys.exit(f"{src}: no function {FN} (not the edgefrag_7228c0_11150 lift)")
if "gow_edge_o11430_11460(" in text:
    open(dst, "w", newline="").write(nl.join(lines))
    sys.exit(print(f"{dst}: the switch is already there; copied"))
hdr = re.compile(r"^void (\w+_spu_func_[0-9A-F]{8})(_impl)?\(spu_context\* ctx\) \{\s*$")
out, cur = [], None
n = {"11460": 0, "back": 0}
for l in lines:
    m = hdr.match(l)
    if m:
        cur = m.group(1)
    if cur == FN and re.match(r"^\s*loc_00011460:", l):
        out += ["        if (__builtin_expect(g_edge_o11430 < 0, 0)) g_edge_o11430 = gow_edge_o11430_mode();",
                "        if (g_edge_o11430) { if (g_edge_o11430 <= 2) { gow_edge_o11430_11460(ctx); goto gow_11460_exit; }"
                " gow_edge_o11430_check_begin(ctx); }"]
        n["11460"] += 1
    out.append(l)
    if cur == FN and l.strip() == BACK:
        out.append("gow_11460_exit:")
        out.append("        if (g_edge_o11430 > 2) gow_edge_o11430_check_end(ctx);")
        n["back"] += 1
if n["11460"] != 1 or n["back"] != 1:
    sys.exit(f"11430 sites: 11460 x{n['11460']}, back edge x{n['back']} (want 1 each)")
decl = ["int gow_edge_o11430_mode(void);",
        "void gow_edge_o11430_11460(spu_context* ctx);",
        "void gow_edge_o11430_check_begin(spu_context* ctx);",
        "void gow_edge_o11430_check_end(spu_context* ctx);",
        "static int g_edge_o11430 = -1;"]
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out[last_inc + 1:last_inc + 1] = decl
open(dst, "w", newline="").write(nl.join(out))
print(f"{dst}: 11430 loop at 11460 switchable by GOW_EDGE_O11430")
