"""level_rate.py <log>...: game frames/s in the level.
- window: [entry+60 s, entry+600 s], entry = the largest 30 s heartbeat step (> 300 game frames).
- play:   from the end of the loading screen to the last heartbeat. The loading screen counts game frames at
          ~17/s, gameplay at ~1-2.5/s, so its end is the last 30 s step above 150 game frames. A window that still
          holds part of the loading screen reads high; this one is gameplay only.
- tail:   [600 s, 1080 s], gameplay in every run that reaches it; the steadiest number for A/B pairs."""
import re, sys
for f in sys.argv[1:]:
    hb = [(int(t), int(g)) for t, g in re.findall(r'\[heartbeat\] t=(\d+)s \S+ game_frame=(\d+)', open(f, errors='replace').read())]
    steps = [(hb[i][1] - hb[i-1][1], hb[i][0]) for i in range(1, len(hb))]
    entry = max(steps)[1] if steps and max(steps)[0] > 300 else None
    if entry is None: print(f"{f}: no level entry"); continue
    g = dict(hb); a, b = entry + 60, entry + 600
    win = f"{(g[b]-g[a])/(b-a):.3f}" if a in g and b in g else "incomplete"
    load_end = max(t for d, t in steps if d > 150)
    last = hb[-1][0]
    play = f"{(g[last]-g[load_end])/(last-load_end):.3f} from {load_end}s" if last - load_end >= 120 else "too short"
    tail = f"{(g[1080]-g[600])/480:.3f}" if 600 in g and 1080 in g and load_end <= 600 else "n/a"
    print(f"{f}: entry {entry}s  window {win}  play {play}  tail {tail} frames/s")
