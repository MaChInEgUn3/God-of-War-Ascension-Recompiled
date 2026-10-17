#!/usr/bin/env python3
"""Turn SPU_INTERP_MISS logs into per-module extra entries for lift_jobmods.py.

Reads one or more run logs with `[interp-miss]   img=<id> pc=0x<pc> n=<count>`
lines, maps overlay image ids (200+) to module names through the generated
jobmods_register.c, and merges the targets into a JSON file
{"<module name>": ["0x...", ...]} (JOBMOD_EXTRA_FUNCS_JSON). Targets in the
policy module itself (image < 200) are printed, not written: they belong to
that image's own lift.

Usage: python analysis/interp_miss_to_extra.py <register.c> <out.json> <log>...
"""
import json, os, re, sys

def main():
    reg, out, logs = sys.argv[1], sys.argv[2], sys.argv[3:]
    img2mod = {int(i): m for i, m in
               re.findall(r"spu_begin_image\((\d+)\); (\w+?)_spu_recomp_register", open(reg).read())}
    extra = json.load(open(out)) if os.path.exists(out) else {}
    other = set()
    for log in logs:
        for img, pc in re.findall(r"\[interp-miss\]\s+(?:wid=-?\d+ )?img=(\d+) pc=0x([0-9A-Fa-f]+)",
                                  open(log, encoding="latin1").read()):
            img, pc = int(img), int(pc, 16)
            if img in img2mod:
                lst = extra.setdefault(img2mod[img], [])
                if hex(pc) not in lst:
                    lst.append(hex(pc))
            else:
                other.add((img, pc))
    json.dump(extra, open(out, "w"), indent=1, sort_keys=True)
    print(f"{sum(len(v) for v in extra.values())} entries in {len(extra)} modules -> {out}")
    for img, pc in sorted(other):
        print(f"  not a job module: img={img} pc=0x{pc:05X}")

if __name__ == "__main__":
    main()
