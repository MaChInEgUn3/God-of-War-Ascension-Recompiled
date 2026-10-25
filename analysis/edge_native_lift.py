"""edge_native_lift.py <spu_recomp.c> <out.c>: a lab lift of the EDGE job module (d935) whose E550 loop at
loc_0000EC08 can run natively (port gow_edge_native.c), chosen at run time by GOW_EDGE_NATIVE.

In jobmod_d93503647dd8_e6d98_spu_func_0000E550 (or its _impl, so it composes with spu_fnprobe_lift.py in either
order):
- just before `loc_0000EC08:` (the loop is entered only by falling through; the back edge jumps to the label, so
  this runs once per loop entry and costs the lifted path nothing per iteration):
      if (g_edge_native) { if (g_edge_native <= 2) { gow_edge_e550_ec08(ctx); goto loc_0000EDE8; }
                           gow_edge_e550_check_begin(ctx); }
- just after `loc_0000EDE8:`: in the check modes (3, 4), gow_edge_e550_check_end(ctx) compares the lifted
  loop's result with the native one (EDE8 is also reached from the EA7C loop; check_end ignores that).
g_edge_native is read once from the env (gow_edge_native_mode). Unset/0: the lifted loop, so one lab exe A/Bs
itself. Lab builds only (work/buildlab.sh with LAB_LIFT); the production lift never calls the native file.
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
hdr = re.compile(r"^void \w+_spu_func_([0-9A-F]{8})(_impl)?\(spu_context\* ctx\) \{\s*$")
out, cur, n_ec08, n_ede8 = [], None, 0, 0
for l in lines:
    m = hdr.match(l)
    if m:
        cur = m.group(1)
    if cur == "0000E550" and re.match(r"^\s*loc_0000EC08:", l):
        out += ["        if (__builtin_expect(g_edge_native < 0, 0)) g_edge_native = gow_edge_native_mode();",
                "        if (g_edge_native) { if (g_edge_native <= 2) { gow_edge_e550_ec08(ctx); goto loc_0000EDE8; }"
                " gow_edge_e550_check_begin(ctx); }"]
        n_ec08 += 1
        out.append(l)
        continue
    out.append(l)
    if cur == "0000E550" and re.match(r"^\s*loc_0000EDE8:", l):
        out.append("        if (g_edge_native > 2) gow_edge_e550_check_end(ctx);")
        n_ede8 += 1
if n_ec08 != 1 or n_ede8 != 1:
    sys.exit(f"E550 labels: EC08 x{n_ec08}, EDE8 x{n_ede8} (want 1 each)")
decl = ["int gow_edge_native_mode(void);",
        "void gow_edge_e550_ec08(spu_context* ctx);",
        "void gow_edge_e550_check_begin(spu_context* ctx);",
        "void gow_edge_e550_check_end(spu_context* ctx);",
        "static int g_edge_native = -1;"]
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out[last_inc + 1:last_inc + 1] = decl
open(dst, "w", newline="").write(nl.join(out))
print(f"{dst}: E550 loop at EC08 switchable by GOW_EDGE_NATIVE")
