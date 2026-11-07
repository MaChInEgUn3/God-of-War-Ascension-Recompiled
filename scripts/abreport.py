#!/usr/bin/env python3
"""Reads the [ab] lines of a GOW_AB run (work/port/main.cpp): the lever goes on, off, off, on, on, off...
Each on,off,off,on block (windows 4k..4k+3) gives one difference in ms per game frame, on minus off, so a
linear drift of the scene cancels inside the block. Prints the median and quartiles of those differences, the
share of blocks that favour the lever, and both sides' totals.

usage: abreport.py <log> [more logs...]
"""
import re
import statistics
import sys

PAT = re.compile(r'\[ab\] lever=(\S+) w=(\d+) on=(\d) frames=(\d+) secs=([\d.]+)'
                 r'(?: runs=(\d+) native=(\d+) us_run=([\d.]+))?'
                 r'(?: draws=(\d+) us_draw=([\d.]+) rsx_busy=([\d.]+))?')


def windows(path):
    rows = {}
    with open(path, errors='replace') as f:
        for line in f:
            m = PAT.search(line)
            if m:
                rows[int(m.group(2))] = (m.group(1), int(m.group(3)), int(m.group(4)), float(m.group(5)),
                                        int(m.group(6) or 0), int(m.group(7) or 0), float(m.group(8) or 0),
                                        int(m.group(9) or 0), float(m.group(10) or 0), float(m.group(11) or 0))
    return rows


def report(path):
    rows = windows(path)
    if not rows:
        print(f'{path}: no [ab] lines')
        return
    lever = next(iter(rows.values()))[0]
    diffs = []
    tot = {0: [0, 0.0], 1: [0, 0.0]}
    for k in range(0, max(rows) + 1, 4):
        block = [rows.get(k + i) for i in range(4)]
        if not all(block) or any(b[2] == 0 for b in block):
            continue
        ms = {0: [], 1: []}
        for _, on, frames, secs, *_ in block:
            ms[on].append(1000.0 * secs / frames)
            tot[on][0] += frames
            tot[on][1] += secs
        diffs.append(statistics.mean(ms[1]) - statistics.mean(ms[0]))
    if len(diffs) < 3:
        print(f'{path}: lever={lever}, only {len(diffs)} complete blocks')
        return
    q = statistics.quantiles(diffs, n=4)
    won = sum(d < 0 for d in diffs)
    print(f'{path}: lever={lever} blocks={len(diffs)}')
    print(f'  on-off ms/frame: median {statistics.median(diffs):+.2f} quartiles {q[0]:+.2f} / {q[2]:+.2f}'
          f' lever faster in {won}/{len(diffs)}')
    for on in (1, 0):
        f, s = tot[on]
        if f:
            print(f'  on={on}: {f} frames in {s:.0f} s = {f / s:.2f} fps, {1000 * s / f:.2f} ms/frame')
    us = {0: [], 1: []}
    runs = {0: 0, 1: 0}
    nat = {0: 0, 1: 0}
    for r in rows.values():
        if r[4]:
            us[r[1]].append(r[6])
            runs[r[1]] += r[4]
            nat[r[1]] += r[5]
    if us[0] and us[1]:
        print(f'  EDGE module run: on {statistics.median(us[1]):.2f} us, off {statistics.median(us[0]):.2f} us (window medians);'
              f' native share on {100 * nat[1] / max(1, runs[1]):.0f}%, off {100 * nat[0] / max(1, runs[0]):.0f}%')

    ud = {0: [], 1: []}
    busy = {0: [], 1: []}
    for r in rows.values():
        if r[7]:
            ud[r[1]].append(r[8])
            busy[r[1]].append(r[9])
    if ud[0] and ud[1]:
        print(f'  walker: on {statistics.median(ud[1]):.2f} us a draw, off {statistics.median(ud[0]):.2f} (window medians);'
              f' busy on {statistics.median(busy[1]):.2f}, off {statistics.median(busy[0]):.2f}')


for p in sys.argv[1:]:
    report(p)
