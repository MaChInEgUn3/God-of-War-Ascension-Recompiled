#!/usr/bin/env python3
"""Per-job cost of the EDGE workloads (wids 15-17) from SPURS_PM_PROF=60 lines.

A [pm-prof] field reads w<id>=<busy%>/<runs>...j<jobs>...; busy% of the 60 s window
divided by the jobs run in it is the mean time per job. Frame rate is a poor A/B
metric here (two runs reach the level at different times and in different scenes);
this one only counts windows where EDGE is busy (>= --min-busy %).

Usage: python analysis/pmprof_edge.py <log>... [--min-busy 15]
"""
import re, sys

def main():
    args, min_busy = [], 15
    it = iter(sys.argv[1:])
    for a in it:
        if a == "--min-busy":
            min_busy = int(next(it))
        else:
            args.append(a)
    for log in args:
        text = open(log, encoding="latin1").read().replace("\r", "\n")
        rows = []
        for line in text.split("\n"):
            i = line.find("[pm-prof]")
            if i < 0:
                continue
            line = line[i:]
            for wid in ("15", "16", "17"):
                m = re.search(r"\bw%s=(\d+)%%/\d+\S*?j(\d+)" % wid, line)
                if m and int(m.group(1)) >= min_busy and int(m.group(2)):
                    rows.append(int(m.group(1)) * 600.0 / int(m.group(2)))
        if rows:
            rows.sort()
            print(f"{log}: {len(rows)} busy windows, ms/job median {rows[len(rows)//2]:.3f} "
                  f"min {rows[0]:.3f} max {rows[-1]:.3f}")
        else:
            print(f"{log}: no busy EDGE windows")

if __name__ == "__main__":
    main()
