#!/usr/bin/env python3
"""Game frames/s between two heartbeat times. usage: hbrate.py <lo s> <hi s> <log> [more logs]"""
import re
import sys

lo, hi = int(sys.argv[1]), int(sys.argv[2])
for p in sys.argv[3:]:
    rows = []
    last = 0
    for line in open(p, errors='replace'):
        m = re.search(r'\[heartbeat\] t=(\d+)s flips=\d+ game_frame=(\d+)', line)
        if m:
            rows.append((int(m.group(1)), int(m.group(2))))
    a = [r for r in rows if lo <= r[0] <= hi]
    if len(a) < 2:
        print(p, 'too short', rows[-1:] )
        continue
    print(f'{p}: {(a[-1][1] - a[0][1]) / (a[-1][0] - a[0][0]):.2f} gf/s over {a[0][0]}-{a[-1][0]} s, last t={rows[-1][0]}')
