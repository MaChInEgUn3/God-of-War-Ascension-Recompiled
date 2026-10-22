"""level_rate.py <log>...: game frames/s in the level, over [entry+60 s, entry+600 s].
Entry = the largest 30 s heartbeat step (> 300 game frames) (the level load burst)."""
import re, sys
for f in sys.argv[1:]:
    hb = [(int(t), int(g)) for t, g in re.findall(r'\[heartbeat\] t=(\d+)s \S+ game_frame=(\d+)', open(f, errors='replace').read())]
    steps = [(hb[i][1] - hb[i-1][1], hb[i][0]) for i in range(1, len(hb))]
    entry = max(steps)[1] if steps and max(steps)[0] > 300 else None
    if entry is None: print(f"{f}: no level entry"); continue
    g = dict(hb); a, b = entry + 60, entry + 600
    if a not in g or b not in g: print(f"{f}: entry {entry}s, window incomplete (last t={hb[-1][0]})"); continue
    print(f"{f}: entry {entry}s  {(g[b]-g[a])/(b-a):.3f} frames/s")
