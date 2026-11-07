#!/usr/bin/env python3
"""Reads the native job driver's lines from run logs. usage: wwsread.py <log> [more logs]"""
import re
import sys

for p in sys.argv[1:]:
    s = open(p, errors='replace').read()
    print('==', p, 'CHECK FAILED', s.count('CHECK FAILED'), 'clock-watch', s.count('[clock-watch]'),
          'last hb', re.findall(r'\[heartbeat\] t=(\d+)s', s)[-1:])
    for l in s.split('\n'):
        if 'CHECK FAILED' in l:
            print('  ', l[:200])
            break
    w = [l for l in s.split('\n') if l.startswith('[wws-native]')]
    seen = {}
    for l in w:
        m = re.match(r'\[wws-native\] (w\d+):', l)
        if m:
            seen[m.group(1)] = l
    for k in sorted(seen):
        print('  ', seen[k][:520])
    b = [l for l in s.split('\n') if 'total=' in l and 'idle=' in l and l.startswith('[pm-prof]')]
    for l in b[-3:]:
        print('  ', ' '.join(re.findall(r'w1[567]=\S+', l)), re.findall(r'total=\S+', l))
