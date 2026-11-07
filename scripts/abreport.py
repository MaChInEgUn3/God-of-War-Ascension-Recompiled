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

PAT = re.compile(r'\[ab\] lever=(\S+) w=(\d+) on=(\d) frames=(\d+) secs=([\d.]+)')


def windows(path):
    rows = {}
    with open(path, errors='replace') as f:
        for line in f:
            m = PAT.search(line)
            if m:
                rows[int(m.group(2))] = (m.group(1), int(m.group(3)), int(m.group(4)), float(m.group(5)))
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
        for _, on, frames, secs in block:
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


for p in sys.argv[1:]:
    report(p)
