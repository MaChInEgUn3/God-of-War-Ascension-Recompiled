"""gen_a67e_noise_lift.py <spu_recomp.c> <out.c>: the General6 job module (a67e) lift with a switch in front of
function 4930 (Perlin noise, native in gow_a67e_noise.c). The lifted 4930 is renamed ..._4930_lifted; a new
..._4930 picks the path by GOW_A67_NOISE (gow_a67e_noise_mode): 1/unset native, 0 lifted, 3 check. Callers
and the dispatch table keep the original name. The output is game code and is not committed."""
import sys

FN = "jobmod_a67e549fa0ab_e41e8_spu_func_00004930"
src, dst = sys.argv[1], sys.argv[2]
t = open(src, newline="").read()
nl = "\r\n" if "\r\n" in t else "\n"
head = "void %s(spu_context* ctx) {" % FN
if t.count(head) != 1:
    sys.exit("gen_a67e_noise_lift: expected one definition of %s" % FN)
# the block 4930 falls into must still be the straight-line noise block the native code reproduces
if "jobmod_a67e549fa0ab_e41e8_spu_func_00004AB0" not in t or "spu_ila(0x4680)" not in t:
    sys.exit("gen_a67e_noise_lift: the a67e lift no longer looks like the one gow_a67e_noise.c was written from")
wrapper = nl.join([
    "int gow_a67e_noise_mode(void);",
    "void gow_a67e_noise(spu_context* ctx);",
    "void gow_a67e_noise_check(spu_context* ctx, void (*lifted)(spu_context*));",
    "void %s_lifted(spu_context* ctx);" % FN,
    "void %s(spu_context* ctx) {" % FN,
    "    static int m = -1;",
    "    if (m < 0) m = gow_a67e_noise_mode();",
    "    if (m == 1) { gow_a67e_noise(ctx); SPU_RET(ctx); }",
    "    if (m == 3) { gow_a67e_noise_check(ctx, %s_lifted); return; }" % FN,
    "#ifdef SPU_DIRECT_TAILCALLS",
    "    __attribute__((musttail)) return %s_lifted(ctx);" % FN,
    "#else",
    "    %s_lifted(ctx);" % FN,
    "#endif",
    "}",
    "",
])
t = t.replace(head, wrapper + "void %s_lifted(spu_context* ctx) {" % FN)
open(dst, "w", newline="").write(t)
