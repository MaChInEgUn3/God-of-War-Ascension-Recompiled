#!/usr/bin/env python3
"""Print the header and an event summary of an SPU job capture file.

Usage: jobcap_dump.py <job_<wid>_<seq>.bin> [-v]

The format is SPU_JOB_CAPTURE's (toolkit runtime/spu/spu_channels.c, header
comment of the "SPU job capture" block; docs/EDGE_NATIVE.md). -v lists every
event instead of only the counts.
"""
import struct
import sys
from collections import Counter

TYPES = {1: "GET", 2: "PUT", 3: "GETLLAR", 4: "PUTLLC", 5: "PUTLLUC",
         6: "RDCH", 7: "POLL", 8: "PMCALL", 15: "END"}
END_REASONS = {1: "return to link", 2: "halt", 3: "next job start"}
HDR_FIELDS = ("version header_size ls_size gpr_count wid image_id resident_ovl "
              "resident_task spu_id policy_mode entry_pc link seq ms code_slots").split()


def main():
    path = sys.argv[1]
    verbose = "-v" in sys.argv[2:]
    data = open(path, "rb").read()
    if data[:8] != b"SPUJCAP\0":
        sys.exit(f"{path}: not a job capture (magic {data[:8]!r})")
    vals = struct.unpack_from("<5I3iIi5I", data, 8)
    h = dict(zip(HDR_FIELDS, vals))
    print(f"{path}: {len(data)} bytes")
    print(f"  wid={h['wid']} seq={h['seq']} image_id={h['image_id']} resident_ovl={h['resident_ovl']} "
          f"resident_task={h['resident_task']} spu_id=0x{h['spu_id']:X} policy_mode={h['policy_mode']}")
    print(f"  entry pc=0x{h['entry_pc']:05X} link=0x{h['link']:05X} t={h['ms'] / 1000:.1f}s "
          f"(version {h['version']}, header {h['header_size']} B)")
    for s in range(h["code_slots"]):
        lsa, size, src, img = struct.unpack_from("<3Ii", data, 68 + 16 * s)
        if img:
            print(f"  resident_code[{s}]: lsa=0x{lsa:05X} size=0x{size:X} src=0x{src:08X} image {img}")
    off = h["header_size"] + h["ls_size"] + 16 * h["gpr_count"]
    count, nbytes, ea_lo = Counter(), Counter(), {}
    end = None
    while off + 32 <= len(data):
        typ, n, pc, a, b, c, d, idx = struct.unpack_from("<8I", data, off)
        name = TYPES.get(typ, f"type{typ}")
        count[name] += 1
        nbytes[name] += n
        if typ in (1, 2):
            lo, hi = ea_lo.get(name, (b, b + d))
            ea_lo[name] = (min(lo, b), max(hi, b + d))
        if verbose:
            if typ in (1, 2):
                desc = f"cmd=0x{a:X} ea=0x{b:08X} lsa=0x{c:05X} size={d}"
            elif typ in (3, 4, 5):
                desc = f"cmd=0x{a:X} ea=0x{b:08X} lsa=0x{c:05X}" + (f" ok={d}" if typ != 3 else "")
            elif typ == 6:
                desc = f"ch={a} value={data[off + 32:off + 48].hex()}"
            elif typ == 7:
                desc = f"r3=0x{a:08X}"
            elif typ == 8:
                desc = f"target=0x{a:05X} link=0x{b:05X}"
            else:
                desc = f"a=0x{a:X} b=0x{b:X} c=0x{c:X} d=0x{d:X}"
            print(f"  #{idx:5d} pc=0x{pc:05X} {name:8s} {desc}")
        if typ == 15:
            end = (a, b, c, d, idx)
        off += 32 + n
    if off != len(data):
        print(f"  WARNING: {len(data) - off} trailing bytes (truncated record?)")
    print("  events: " + ", ".join(f"{k}={count[k]} ({nbytes[k]} B)" for k in sorted(count)))
    for k, (lo, hi) in sorted(ea_lo.items()):
        print(f"  {k} ea span 0x{lo:08X}..0x{hi:08X}")
    if end:
        reason, pc, r0, ovl, idx = end
        print(f"  END: {END_REASONS.get(reason, reason)} at pc=0x{pc:05X} r0=0x{r0:05X} "
              f"resident_ovl={ovl} after {idx} events")
    else:
        print("  no END record (capture not closed: process ended mid-job)")


if __name__ == "__main__":
    main()
