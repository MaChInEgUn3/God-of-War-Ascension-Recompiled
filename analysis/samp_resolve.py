#!/usr/bin/env python3
"""Resolve PS3_SAMPLE_HOST=1 "[samp] rva 0x... N% (count)" lines against a link map.

usage: samp_resolve.py <run.log> <MyGameRecomp.map> [report#|-1] [top]

The sampler buckets host RIPs by 16-byte RVA. Each bucket is credited to the
nearest public symbol at or below it (link.exe / lld-link /MAP "Publics by
Value"), then summed per symbol. report# picks which periodic report to use
(-1 = last, "all" = sum of every report, "t<sec>" = sum of reports at or after
<sec> seconds). Host mode lines carry "tid N": a per-thread top list follows.
"""
import bisect
import re
import sys
from collections import Counter


def load_map(path):
    base = None
    syms = []
    pub = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s")
    for line in open(path, errors="replace"):
        m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", line)
        if m:
            base = int(m.group(1), 16)
            continue
        m = pub.match(line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
    if base is None:
        base = 0x140000000
    syms = sorted((a - base, n) for a, n in syms if a >= base)
    return [a for a, _ in syms], [n for _, n in syms]


def reports(log):
    cur = None
    out = []
    for line in open(log, errors="replace"):
        m = re.search(r"\[samp\] --- report at t=(\d+)ms", line)
        if m:
            cur = Counter()
            out.append((int(m.group(1)) / 1000.0, cur))
            continue
        m = re.match(r"\[samp\] rva 0x([0-9A-Fa-f]+)\s+[\d.]+% \((\d+)\)(?: tid (\d+))?", line)
        if m and cur is not None:
            cur[(int(m.group(3) or 0), int(m.group(1), 16))] += int(m.group(2))
    return out


def main():
    log, mp = sys.argv[1], sys.argv[2]
    which = sys.argv[3] if len(sys.argv) > 3 else "-1"
    top = int(sys.argv[4]) if len(sys.argv) > 4 else 40
    addrs, names = load_map(mp)
    reps = [(t, r) for t, r in reports(log) if r]
    if not reps:
        sys.exit("no [samp] rva lines (run with PS3_SAMPLE=<ms> PS3_SAMPLE_HOST=1)")
    rvas = Counter()
    if which == "all" or which.startswith("t"):
        t0 = float(which[1:]) if which.startswith("t") else -1.0
        for t, r in reps:
            if t >= t0:
                rvas.update(r)
    else:
        rvas = reps[int(which)][1]
    per = Counter()
    by_tid = {}
    for (tid, rva), n in rvas.items():
        i = bisect.bisect_right(addrs, rva) - 1
        name = names[i] if i >= 0 else "?"
        per[name] += n
        by_tid.setdefault(tid, Counter())[name] += n
    tot = sum(per.values())
    print(f"{tot} samples in {len(reps)} report(s); top {top}:")
    for name, n in per.most_common(top):
        print(f"{100.0 * n / tot:6.2f}%  {n:7d}  {name}")
    if len(by_tid) > 1:  # host mode with per-thread keys
        for tid, c in sorted(by_tid.items(), key=lambda kv: -sum(kv[1].values())):
            t = sum(c.values())
            if t < tot // 50:
                continue
            print(f"--- tid {tid}: {t} samples ({100.0 * t / tot:.1f}%)")
            for name, n in c.most_common(12):
                print(f"   {100.0 * n / t:6.2f}%  {n:7d}  {name}")


if __name__ == "__main__":
    main()
