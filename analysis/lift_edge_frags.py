#!/usr/bin/env python3
"""Lift the EBOOT code fragments GoW:A's EDGE jobs stage in local store.

The EDGE jobs (wids 15-17) DMA small code fragments out of the EBOOT and pack them
one after another from LS 0x11090, so a fragment lands at one of a few addresses.
Lifted code is keyed by LS address, so each (source EA, LSA) placement is lifted on
its own and registered with spu_overlay_register_region_at(): the runtime marks it
resident when that exact GET happens and dispatches the span to it.

Inputs: run logs with SPU_LSA_GETLOG lines (and/or a previous edgefrags_register.c, whose
placements are kept, so a re-lift from new logs never drops one)
  [lsa-get] wid=<w> img=<i> lsa=0x<lsa> ea=0x<ea> size=0x<n> ...
and optionally SPU_INTERP_MISS logs, whose targets inside a placement become extra
function entries. The function entries of an existing lift of a placement (from the misses of older
logs) are passed again, so a re-lift keeps them. Output: <port>/spu/edgefrag_<ea>_<lsa>/spu_recomp.{c,h} and
<port>/spu/edgefrags_register.c (image ids 300+). Derived from the game: keep it out
of the repository.

Usage: python analysis/lift_edge_frags.py [--merge-chunks] <EBOOT.elf> <port_dir> <ps3recomp_dir> <log or .c>...
--merge-chunks is passed through to spu_lifter.py (gotos instead of trampolines between chunks).
Copy port/spu/edgefrags_register.c aside first: it is rewritten.
"""
import os, re, subprocess, sys

WIDS = {15, 16, 17}           # EDGE0/1/2
EBOOT_LO, EBOOT_HI = 0x10000, 0x820000
MAX_SIZE = 0x2000             # code fragments; the 4-16 KB tables at LS 0x10000 are data
IMAGE_BASE = 300
# Lifts the port patches at build time (port/gen_edge_frag_lift.py, gen_edge_o11430_body.py expect their block
# layout; a new entry inside the patched loop breaks them): an existing lift of these is kept as it is.
PINNED = {"edgefrag_7228c0_11150"}

def main():
    args = [a for a in sys.argv[1:] if a != "--merge-chunks"]
    merge = len(args) != len(sys.argv) - 1
    elf, port, sdk, logs = args[0], args[1], args[2], args[3:]
    data = open(elf, "rb").read()
    py = sys.executable
    frags, misses = {}, set()
    for log in logs:
        t = open(log, encoding="latin1").read()
        if log.endswith(".c"):   # a previous edgefrags_register.c: keep its placements
            for ea, lsa, n in re.findall(r"spu_overlay_register_region_at\(0x([0-9A-F]+)u, 0x([0-9A-F]+)u, 0x([0-9A-F]+)u", t):
                frags[(int(ea, 16), int(lsa, 16))] = int(n, 16)
            continue
        for w, lsa, ea, n in re.findall(r"\[lsa-get\] wid=(-?\d+) img=\d+ lsa=0x([0-9A-F]+) ea=0x([0-9A-F]+) size=0x([0-9A-F]+)", t):
            w, lsa, ea, n = int(w), int(lsa, 16), int(ea, 16), int(n, 16)
            if w in WIDS and EBOOT_LO <= ea < EBOOT_HI and n <= MAX_SIZE and lsa >= 0x11000:
                frags[(ea, lsa)] = n
        misses |= {int(pc, 16) for pc in re.findall(r"\[interp-miss\]\s+wid=1[567] img=\d+ pc=0x([0-9A-F]+)", t)}
    out_dir = os.path.join(port, "spu")
    reg = []
    for k, ((ea, lsa), n) in enumerate(sorted(frags.items())):
        name = f"edgefrag_{ea:06x}_{lsa:05x}"
        d = os.path.join(out_dir, name)
        if name in PINNED and os.path.exists(os.path.join(d, "spu_recomp.c")):
            reg.append((name, ea, lsa, n, IMAGE_BASE + k))
            print(f"{name}: kept (pinned) image {IMAGE_BASE + k}")
            continue
        os.makedirs(d, exist_ok=True)
        blob = data[ea - 0x10000: ea - 0x10000 + n]
        raw, elf_out = os.path.join(d, name + ".bin"), os.path.join(d, name + ".elf")
        open(raw, "wb").write(blob)
        subprocess.check_call([py, os.path.join(sdk, "tools", "wrap_spu_elf.py"), raw,
                               "--base", hex(lsa), "--entry", hex(lsa), "--out", elf_out],
                              stdout=subprocess.DEVNULL)
        cmd = [py, os.path.join(sdk, "tools", "spu_lifter.py"), "--auto-functions", elf_out,
               "--symbol-prefix", name + "_", "-o", d]
        if merge:
            cmd += ["--merge-chunks"]
        # Keep the entries of an existing lift of this placement (misses from older logs), so a re-lift never drops one.
        old = os.path.join(d, "spu_recomp.c")
        kept = {int(a, 16) for a in re.findall(r"^void \w+_spu_func_([0-9A-F]{8})\(", open(old, encoding="utf8").read(), re.M)} \
            if os.path.exists(old) else set()
        extra = sorted({m for m in misses | kept if lsa <= m < lsa + n})
        if extra:
            cmd += ["--extra-funcs", ",".join(hex(m) for m in extra)]
        subprocess.check_call(cmd, stdout=subprocess.DEVNULL)
        for fn in ("spu_recomp.c", "spu_recomp.h"):
            p = os.path.join(d, fn)
            s = open(p, encoding="utf8").read()
            s = s.replace('"../../runtime/spu/spu_context.h"', '"spu_context.h"')
            s = s.replace('"../../runtime/spu/spu_helpers.h"', '"spu_helpers.h"')
            open(p, "w", encoding="utf8", newline="\n").write(s)
        os.remove(raw); os.remove(elf_out)
        reg.append((name, ea, lsa, n, IMAGE_BASE + k))
        print(f"{name}: ea 0x{ea:06X} -> LS 0x{lsa:05X} size 0x{n:X} image {IMAGE_BASE + k}"
              + (f" (+{len(extra)} entries)" if extra else ""))
    L = ["/* GENERATED by analysis/lift_edge_frags.py -- EDGE code fragments, one lifted",
         " * image per (source EA, local-store placement). */",
         "#include <stdint.h>", "",
         "extern void spu_begin_image(int image_id);",
         "extern void spu_overlay_register_region_at(uint32_t ea, uint32_t lsa, uint32_t span, int image_id);", ""]
    L += [f"extern void {name}_spu_recomp_register(void);" for name, *_ in reg]
    L += ["", "void gow_edgefrags_register_all(void)", "{"]
    for name, ea, lsa, n, iid in reg:
        L.append(f"    spu_begin_image({iid}); {name}_spu_recomp_register();")
        L.append(f"    spu_overlay_register_region_at(0x{ea:08X}u, 0x{lsa:05X}u, 0x{n:X}u, {iid});")
    L += ["    spu_begin_image(0);", "}", "",
          "__attribute__((constructor)) static void gow_edgefrags_register_ctor(void)", "{",
          "    gow_edgefrags_register_all();", "}", ""]
    open(os.path.join(out_dir, "edgefrags_register.c"), "w", encoding="utf8", newline="\n").write("\n".join(L))
    print(f"{len(reg)} placements -> {os.path.join(out_dir, 'edgefrags_register.c')}")

if __name__ == "__main__":
    main()
