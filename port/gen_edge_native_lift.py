"""gen_edge_native_lift.py <spu_recomp.c> <out.c>: the EDGE job module (d935) lift with its native-switchable
regions (port gow_edge_native.c, gow_edge_native_73e0.c), each picked at run time by an env variable.

1. E550's loop at loc_0000EC08, by GOW_EDGE_NATIVE. In jobmod_d93503647dd8_e6d98_spu_func_0000E550 (or its
   _impl, so it composes with spu_fnprobe_lift.py in either order):
   - just before `loc_0000EC08:` (the loop is entered only by falling through; the back edge jumps to the
     label, so this runs once per loop entry and costs the lifted path nothing per iteration):
         if (g_edge_native) { if (g_edge_native <= 2) { gow_edge_e550_ec08(ctx); goto loc_0000EDE8; }
                              gow_edge_e550_check_begin(ctx); }
   - just after `loc_0000EDE8:`: in the check modes (3, 4), gow_edge_e550_check_end(ctx) compares the lifted
     loop's result with the native one (EDE8 is also reached from the EA7C loop; check_end ignores that).
   g_edge_native is read once from the env (gow_edge_native_mode): unset/1 native v3, 0 the lifted loop.
2. 73E0's region from loc_000079B0 (outer loop, entered only by falling through from the 7890 block; the
   back edge jumps to the label) to loc_000081C0, by GOW_EDGE_73E0, the same way:
         if (g_edge_73) { if (g_edge_73 <= 2) { gow_edge_73e0_79b0(ctx); goto loc_000081C0; }
                          gow_edge_73e0_check_begin(ctx); }
   before `loc_000079B0:` and `if (g_edge_73 > 2) gow_edge_73e0_check_end(ctx);` after `loc_000081C0:`
   (also reached by the skip at 7890; check_end ignores that). Only function 73E0's copy: the lift has a
   second copy of these blocks in function 75D8 (an alternate entry), which stays lifted.
   g_edge_73 is read once (gow_edge_73e0_mode): 0 the lifted region, unset/1 native v3 (GOW_EDGE_73E0_V2=1:
   v2), 2 native v1, 3/4 check.

The port's CMakeLists.txt builds the d935 lift through this (output spu_recomp_edge.c beside the lift,
untracked: it is the lift). Each switch is added only if the lift does not have it yet, so a lab lift made
with an earlier version of this script (E550 switch only) still gets the 73E0 one, and a lift that has both
is copied unchanged (work/buildlab.sh LAB_LIFT).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
want_e550 = "gow_edge_e550_ec08(" not in text
want_73e0 = "gow_edge_73e0_79b0(" not in text
if not want_e550 and not want_73e0:
    open(dst, "w", newline="").write(text)
    sys.exit(print(f"{dst}: both switches are already there; copied"))
hdr = re.compile(r"^void \w+_spu_func_([0-9A-F]{8})(_impl)?\(spu_context\* ctx\) \{\s*$")
out, cur = [], None
n = {"EC08": 0, "EDE8": 0, "79B0": 0, "81C0": 0}
for l in lines:
    m = hdr.match(l)
    if m:
        cur = m.group(1)
    if want_e550 and cur == "0000E550" and re.match(r"^\s*loc_0000EC08:", l):
        out += ["        if (__builtin_expect(g_edge_native < 0, 0)) g_edge_native = gow_edge_native_mode();",
                "        if (g_edge_native) { if (g_edge_native <= 2) { gow_edge_e550_ec08(ctx); goto loc_0000EDE8; }"
                " gow_edge_e550_check_begin(ctx); }"]
        n["EC08"] += 1
    if want_73e0 and cur == "000073E0" and re.match(r"^\s*loc_000079B0:", l):
        out += ["        if (__builtin_expect(g_edge_73 < 0, 0)) g_edge_73 = gow_edge_73e0_mode();",
                "        if (g_edge_73) { if (g_edge_73 <= 2) { gow_edge_73e0_79b0(ctx); goto loc_000081C0; }"
                " gow_edge_73e0_check_begin(ctx); }"]
        n["79B0"] += 1
    out.append(l)
    if want_e550 and cur == "0000E550" and re.match(r"^\s*loc_0000EDE8:", l):
        out.append("        if (g_edge_native > 2) gow_edge_e550_check_end(ctx);")
        n["EDE8"] += 1
    if want_73e0 and cur == "000073E0" and re.match(r"^\s*loc_000081C0:", l):
        out.append("        if (g_edge_73 > 2) gow_edge_73e0_check_end(ctx);")
        n["81C0"] += 1
if want_e550 and (n["EC08"] != 1 or n["EDE8"] != 1):
    sys.exit(f"E550 labels: EC08 x{n['EC08']}, EDE8 x{n['EDE8']} (want 1 each)")
if want_73e0 and (n["79B0"] != 1 or n["81C0"] != 1):
    sys.exit(f"73E0 labels: 79B0 x{n['79B0']}, 81C0 x{n['81C0']} (want 1 each)")
decl = []
if want_e550:
    decl += ["int gow_edge_native_mode(void);",
             "void gow_edge_e550_ec08(spu_context* ctx);",
             "void gow_edge_e550_check_begin(spu_context* ctx);",
             "void gow_edge_e550_check_end(spu_context* ctx);",
             "static int g_edge_native = -1;"]
if want_73e0:
    decl += ["int gow_edge_73e0_mode(void);",
             "void gow_edge_73e0_79b0(spu_context* ctx);",
             "void gow_edge_73e0_check_begin(spu_context* ctx);",
             "void gow_edge_73e0_check_end(spu_context* ctx);",
             "static int g_edge_73 = -1;"]
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out[last_inc + 1:last_inc + 1] = decl
open(dst, "w", newline="").write(nl.join(out))
print(f"{dst}: " + ", ".join(s for s, w in (("E550 loop at EC08 switchable by GOW_EDGE_NATIVE", want_e550),
                                          ("73E0 region at 79B0 switchable by GOW_EDGE_73E0", want_73e0)) if w))
