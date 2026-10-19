#!/usr/bin/env python3
"""Per-frame split of the main thread's time: waiting for job lists vs its own work.

Reads a run log made with PS3_POLLTOP=60 and GOW_HEARTBEAT=30. For each 60 s
usleep-poll report it takes the milliseconds slept at the main loop's wait site
(lr 0x003E8388: the frame waits there for the WWS job lists the policy module
completes) and the game frames counted by the heartbeats around that report, and
prints wait_ms/frame and work_ms/frame. Runs reach the level at different times
and in different scenes, so frames per 300 s varies by +-12% between identical
builds; these two numbers are steadier and say which side of the frame moved.

Usage: python analysis/frame_wait.py <log>... [--from 600] [--site 0x003E8388]
"""
import re, sys

def main():
    logs, t_from, site = [], 600, "0x003E8388"
    it = iter(sys.argv[1:])
    for a in it:
        if a == "--from":
            t_from = int(next(it))
        elif a == "--site":
            site = next(it)
        else:
            logs.append(a)
    for log in logs:
        lines = open(log, encoding="latin1").read().replace("\r", "\n").split("\n")
        hb = []                       # (index, t, game_frame)
        reports = []                  # (index, slept_ms at site)
        for i, l in enumerate(lines):
            m = re.search(r"\[heartbeat\] t=(\d+)s flips=\d+ game_frame=(\d+)", l)
            if m:
                hb.append((i, int(m.group(1)), int(m.group(2))))
                continue
            if "usleep poll sites" in l:
                ms = 0
                for k in range(i + 1, min(i + 10, len(lines))):
                    m = re.search(r"(\d+)ms slept\s+tid=\d+\s+lr=(0x[0-9A-Fa-f]+)", lines[k])
                    if m and int(m.group(2), 16) == int(site, 16):
                        ms = int(m.group(1))
                reports.append((i, ms))
        rows = []
        for idx, ms in reports:
            after = [h for h in hb if h[0] > idx]
            if not after:
                continue
            t1, g1 = after[0][1], after[0][2]
            before = [h for h in hb if h[1] <= t1 - 60]
            if not before or t1 < t_from:
                continue
            t0, g0 = before[-1][1], before[-1][2]
            frames = (g1 - g0) * 60.0 / (t1 - t0)
            if frames <= 0:
                continue
            if rows and rows[-1][0] == t1:
                continue
            rows.append((t1, frames, ms / frames, (60000 - ms) / frames))
        print(log)
        for t, f, w, k in rows:
            print(f"  t~{t:4d}s  frames/60s {f:6.1f}  wait_ms/frame {w:6.1f}  work_ms/frame {k:6.1f}")
        if rows:
            ws = sorted(r[2] for r in rows); ks = sorted(r[3] for r in rows)
            print(f"  median wait_ms/frame {ws[len(ws)//2]:.1f}  work_ms/frame {ks[len(ks)//2]:.1f}")

if __name__ == "__main__":
    main()
