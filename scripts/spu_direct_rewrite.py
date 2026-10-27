"""spu_direct_rewrite.py <spu_dir> [--apply]: rewrite lifted SPU sources the way the v0.18 lifter emits them
(tools/spu_lifter.py, SPU_DIRECT): a known-target cross-function tail
    { ctx->pc = 0xX; g_spu_trampoline_fn = P_spu_func_0000000X; return; }
becomes
    { SPU_DIRECT(ctx, 0xX, P_spu_func_0000000X); }
only where the pc and the function's address agree. Keeps <file>.pre_direct beside each changed file."""
import os, re, sys
pat = re.compile(r"\{ ctx->pc = 0x([0-9A-F]+); g_spu_trampoline_fn = ([A-Za-z0-9_]*spu_func_([0-9A-F]{8})); return; \}")
root, apply = sys.argv[1], "--apply" in sys.argv
tot = files = skipped = 0
for d in sorted(os.listdir(root)):
    p = os.path.join(root, d, "spu_recomp.c")
    if not os.path.isfile(p): continue
    t = open(p, newline="").read()
    n = 0
    def rep(m):
        global n, skipped
        if int(m.group(1), 16) != int(m.group(3), 16):
            skipped += 1; return m.group(0)
        n += 1
        return "{ SPU_DIRECT(ctx, 0x%s, %s); }" % (m.group(1), m.group(2))
    t2 = pat.sub(rep, t)
    if n:
        files += 1; tot += n
        if apply:
            if not os.path.exists(p + ".pre_direct"): os.replace(p, p + ".pre_direct")
            open(p, "w", newline="").write(t2)
print(f"{tot} sites in {files} files ({skipped} left: pc and function differ){'' if apply else ' [dry run]'}")
