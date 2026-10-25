"""gen_edge_native_lift.py <spu_recomp.c> <out.c>: the EDGE job module (d935) lift with its native-switchable
regions (port gow_edge_native.c, gow_edge_native_73e0.c, gow_edge_native_d640.c, gow_edge_native_a880.c,
gow_edge_native_f538.c, gow_edge_native_6ac8.c), each picked at run time by an env variable.

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
3. D640's loop at loc_0000D92C (exit loc_0000DB70) and D250's loop at loc_0000D4DC (D640's callee; exit:
   the fall-through after its back edge), both by GOW_EDGE_D640 (g_edge_d640, read once by
   gow_edge_d640_mode at whichever site runs first). Both loops are entered only by falling through; their
   back edges jump to the label. In D640 (or its _impl):
         if (g_edge_d640) { if (g_edge_d640 <= 2) { gow_edge_d640_d92c(ctx); goto loc_0000DB70; }
                            gow_edge_d640_d92c_check_begin(ctx); }
   before `loc_0000D92C:` and `if (g_edge_d640 > 2) gow_edge_d640_d92c_check_end(ctx);` after
   `loc_0000DB70:` (also reached by the skip in the D6C0 block; check_end ignores that). In D250 (or its
   _impl) the same before `loc_0000D4DC:` with gow_edge_d640_d4dc and `goto gow_d4dc_exit;`, and after the
   back edge `if (ctx->gpr[3]._u32[0] != 0) goto loc_0000D4DC;` the label
   `gow_d4dc_exit: if (g_edge_d640 > 2) gow_edge_d640_d4dc_check_end(ctx);`. Only functions D640 and
   D250: the alternate-entry functions D8C8, D92C and D4DC keep their lifted copies.
   g_edge_d640: 0 the lifted loops, unset/1 native (packed), 2 native v1, 3/4 check.
4. A880's loop at loc_0000A950 (exit loc_0000AD48), by GOW_EDGE_A880 (g_edge_a880, read once by
   gow_edge_a880_mode). The loop is entered only by falling through from the A928 block; its back edge
   jumps to the label. In A880 (or its _impl):
         if (g_edge_a880) { if (g_edge_a880 <= 2) { gow_edge_a880_a950(ctx); goto loc_0000AD48; }
                            gow_edge_a880_check_begin(ctx); }
   before `loc_0000A950:` and `if (g_edge_a880 > 2) gow_edge_a880_check_end(ctx);` after
   `loc_0000AD48:` (also reached by the skip in the A928 block when the count is not positive; check_end
   ignores that). Only function A880: the alternate-entry functions A928, A950, AD48, AD4C and AD80 keep
   their lifted copies. g_edge_a880: 0 the lifted loop, unset/1 native (packed), 2 native v1, 3/4 check.
5. F538's loop at loc_0000F620 (exit: the fall-through after its back edge, F538's return), by GOW_EDGE_F538
   (g_edge_f538, read once by gow_edge_f538_mode). The loop is entered only by falling through from F538's
   entry block; its back edge jumps to the label. In F538 (or its _impl):
         if (g_edge_f538) { if (g_edge_f538 <= 2) { gow_edge_f538_f620(ctx); goto gow_f620_exit; }
                            gow_edge_f538_check_begin(ctx); }
   before `loc_0000F620:` and, after the back edge `if (ctx->gpr[37]._u32[0] != 0) goto loc_0000F620;`,
   the label `gow_f620_exit: if (g_edge_f538 > 2) gow_edge_f538_check_end(ctx);`. Only function F538: the
   alternate-entry function F620 keeps its lifted copy. g_edge_f538: 0 the lifted loop, unset/1 native
   (packed), 2 native v1, 3/4 check.
6. 6AC8's loop nest from loc_00006BE0 (outer loop, with the inner loop at loc_00006DE0) to its exit
   loc_00006F98, by GOW_EDGE_6AC8 (g_edge_6ac8, read once by gow_edge_6ac8_mode). The outer loop is entered
   only by falling through from 6AC8's entry block; its back edge jumps to the label. In 6AC8 (or its _impl):
         if (g_edge_6ac8) { if (g_edge_6ac8 <= 2) { gow_edge_6ac8_6be0(ctx); goto loc_00006F98; }
                            gow_edge_6ac8_check_begin(ctx); }
   before `loc_00006BE0:` and `if (g_edge_6ac8 > 2) gow_edge_6ac8_check_end(ctx);` after `loc_00006F98:`
   (also reached by the skip in the entry block when the count is not positive and from the 6FC0 path;
   check_end ignores those). Only function 6AC8: the alternate-entry functions 6BE0, 6DE0, 6F40 and 6F98
   keep their lifted copies. g_edge_6ac8: unset/0 the lifted loop nest (the default for now), 1 native
   (packed), 2 native v1, 3/4 check.

The port's CMakeLists.txt builds the d935 lift through this (output spu_recomp_edge.c beside the lift,
untracked: it is the lift). Each switch is added only if the lift does not have it yet, so a lab lift made
with an earlier version of this script still gets the newer ones, and a lift that has all of them is copied
unchanged (work/buildlab.sh LAB_LIFT).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
want_e550 = "gow_edge_e550_ec08(" not in text
want_73e0 = "gow_edge_73e0_79b0(" not in text
want_d640 = "gow_edge_d640_d92c(" not in text
want_a880 = "gow_edge_a880_a950(" not in text
want_f538 = "gow_edge_f538_f620(" not in text
want_6ac8 = "gow_edge_6ac8_6be0(" not in text
if not want_e550 and not want_73e0 and not want_d640 and not want_a880 and not want_f538 and not want_6ac8:
    open(dst, "w", newline="").write(text)
    sys.exit(print(f"{dst}: all switches are already there; copied"))
hdr = re.compile(r"^void \w+_spu_func_([0-9A-F]{8})(_impl)?\(spu_context\* ctx\) \{\s*$")
D4DC_BACK = "if (ctx->gpr[3]._u32[0] != 0) goto loc_0000D4DC;"
F620_BACK = "if (ctx->gpr[37]._u32[0] != 0) goto loc_0000F620;"
out, cur = [], None
n = {"EC08": 0, "EDE8": 0, "79B0": 0, "81C0": 0, "D92C": 0, "DB70": 0, "D4DC": 0, "D4DC_back": 0,
     "A950": 0, "AD48": 0, "F620": 0, "F620_back": 0, "6BE0": 0, "6F98": 0}
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
    if want_d640 and cur == "0000D640" and re.match(r"^\s*loc_0000D92C:", l):
        out += ["        if (__builtin_expect(g_edge_d640 < 0, 0)) g_edge_d640 = gow_edge_d640_mode();",
                "        if (g_edge_d640) { if (g_edge_d640 <= 2) { gow_edge_d640_d92c(ctx); goto loc_0000DB70; }"
                " gow_edge_d640_d92c_check_begin(ctx); }"]
        n["D92C"] += 1
    if want_d640 and cur == "0000D250" and re.match(r"^\s*loc_0000D4DC:", l):
        out += ["        if (__builtin_expect(g_edge_d640 < 0, 0)) g_edge_d640 = gow_edge_d640_mode();",
                "        if (g_edge_d640) { if (g_edge_d640 <= 2) { gow_edge_d640_d4dc(ctx); goto gow_d4dc_exit; }"
                " gow_edge_d640_d4dc_check_begin(ctx); }"]
        n["D4DC"] += 1
    if want_a880 and cur == "0000A880" and re.match(r"^\s*loc_0000A950:", l):
        out += ["        if (__builtin_expect(g_edge_a880 < 0, 0)) g_edge_a880 = gow_edge_a880_mode();",
                "        if (g_edge_a880) { if (g_edge_a880 <= 2) { gow_edge_a880_a950(ctx); goto loc_0000AD48; }"
                " gow_edge_a880_check_begin(ctx); }"]
        n["A950"] += 1
    if want_f538 and cur == "0000F538" and re.match(r"^\s*loc_0000F620:", l):
        out += ["        if (__builtin_expect(g_edge_f538 < 0, 0)) g_edge_f538 = gow_edge_f538_mode();",
                "        if (g_edge_f538) { if (g_edge_f538 <= 2) { gow_edge_f538_f620(ctx); goto gow_f620_exit; }"
                " gow_edge_f538_check_begin(ctx); }"]
        n["F620"] += 1
    if want_6ac8 and cur == "00006AC8" and re.match(r"^\s*loc_00006BE0:", l):
        out += ["        if (__builtin_expect(g_edge_6ac8 < 0, 0)) g_edge_6ac8 = gow_edge_6ac8_mode();",
                "        if (g_edge_6ac8) { if (g_edge_6ac8 <= 2) { gow_edge_6ac8_6be0(ctx); goto loc_00006F98; }"
                " gow_edge_6ac8_check_begin(ctx); }"]
        n["6BE0"] += 1
    out.append(l)
    if want_6ac8 and cur == "00006AC8" and re.match(r"^\s*loc_00006F98:", l):
        out.append("        if (g_edge_6ac8 > 2) gow_edge_6ac8_check_end(ctx);")
        n["6F98"] += 1
    if want_a880 and cur == "0000A880" and re.match(r"^\s*loc_0000AD48:", l):
        out.append("        if (g_edge_a880 > 2) gow_edge_a880_check_end(ctx);")
        n["AD48"] += 1
    if want_e550 and cur == "0000E550" and re.match(r"^\s*loc_0000EDE8:", l):
        out.append("        if (g_edge_native > 2) gow_edge_e550_check_end(ctx);")
        n["EDE8"] += 1
    if want_73e0 and cur == "000073E0" and re.match(r"^\s*loc_000081C0:", l):
        out.append("        if (g_edge_73 > 2) gow_edge_73e0_check_end(ctx);")
        n["81C0"] += 1
    if want_d640 and cur == "0000D640" and re.match(r"^\s*loc_0000DB70:", l):
        out.append("        if (g_edge_d640 > 2) gow_edge_d640_d92c_check_end(ctx);")
        n["DB70"] += 1
    if want_d640 and cur == "0000D250" and l.strip() == D4DC_BACK:
        out.append("gow_d4dc_exit:")
        out.append("        if (g_edge_d640 > 2) gow_edge_d640_d4dc_check_end(ctx);")
        n["D4DC_back"] += 1
    if want_f538 and cur == "0000F538" and l.strip() == F620_BACK:
        out.append("gow_f620_exit:")
        out.append("        if (g_edge_f538 > 2) gow_edge_f538_check_end(ctx);")
        n["F620_back"] += 1
if want_e550 and (n["EC08"] != 1 or n["EDE8"] != 1):
    sys.exit(f"E550 labels: EC08 x{n['EC08']}, EDE8 x{n['EDE8']} (want 1 each)")
if want_73e0 and (n["79B0"] != 1 or n["81C0"] != 1):
    sys.exit(f"73E0 labels: 79B0 x{n['79B0']}, 81C0 x{n['81C0']} (want 1 each)")
if want_d640 and any(n[k] != 1 for k in ("D92C", "DB70", "D4DC", "D4DC_back")):
    sys.exit("D640/D250 sites: " + ", ".join(f"{k} x{n[k]}" for k in ("D92C", "DB70", "D4DC", "D4DC_back")) +
             " (want 1 each)")
if want_a880 and (n["A950"] != 1 or n["AD48"] != 1):
    sys.exit(f"A880 labels: A950 x{n['A950']}, AD48 x{n['AD48']} (want 1 each)")
if want_f538 and (n["F620"] != 1 or n["F620_back"] != 1):
    sys.exit(f"F538 sites: F620 x{n['F620']}, back edge x{n['F620_back']} (want 1 each)")
if want_6ac8 and (n["6BE0"] != 1 or n["6F98"] != 1):
    sys.exit(f"6AC8 labels: 6BE0 x{n['6BE0']}, 6F98 x{n['6F98']} (want 1 each)")
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
if want_d640:
    decl += ["int gow_edge_d640_mode(void);",
             "void gow_edge_d640_d92c(spu_context* ctx);",
             "void gow_edge_d640_d92c_check_begin(spu_context* ctx);",
             "void gow_edge_d640_d92c_check_end(spu_context* ctx);",
             "void gow_edge_d640_d4dc(spu_context* ctx);",
             "void gow_edge_d640_d4dc_check_begin(spu_context* ctx);",
             "void gow_edge_d640_d4dc_check_end(spu_context* ctx);",
             "static int g_edge_d640 = -1;"]
if want_a880:
    decl += ["int gow_edge_a880_mode(void);",
             "void gow_edge_a880_a950(spu_context* ctx);",
             "void gow_edge_a880_check_begin(spu_context* ctx);",
             "void gow_edge_a880_check_end(spu_context* ctx);",
             "static int g_edge_a880 = -1;"]
if want_f538:
    decl += ["int gow_edge_f538_mode(void);",
             "void gow_edge_f538_f620(spu_context* ctx);",
             "void gow_edge_f538_check_begin(spu_context* ctx);",
             "void gow_edge_f538_check_end(spu_context* ctx);",
             "static int g_edge_f538 = -1;"]
if want_6ac8:
    decl += ["int gow_edge_6ac8_mode(void);",
             "void gow_edge_6ac8_6be0(spu_context* ctx);",
             "void gow_edge_6ac8_check_begin(spu_context* ctx);",
             "void gow_edge_6ac8_check_end(spu_context* ctx);",
             "static int g_edge_6ac8 = -1;"]
last_inc = max(i for i, l in enumerate(out[:200]) if l.startswith("#include"))
out[last_inc + 1:last_inc + 1] = decl
open(dst, "w", newline="").write(nl.join(out))
print(f"{dst}: " + ", ".join(s for s, w in (("E550 loop at EC08 switchable by GOW_EDGE_NATIVE", want_e550),
                                          ("73E0 region at 79B0 switchable by GOW_EDGE_73E0", want_73e0),
                                          ("D640 loop at D92C and D250 loop at D4DC switchable by GOW_EDGE_D640",
                                           want_d640),
                                          ("A880 loop at A950 switchable by GOW_EDGE_A880", want_a880),
                                          ("F538 loop at F620 switchable by GOW_EDGE_F538", want_f538),
                                          ("6AC8 loop nest at 6BE0 switchable by GOW_EDGE_6AC8", want_6ac8))
                                 if w))
