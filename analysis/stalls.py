"""stalls.py <log glob>...: stalls from the "[flip] N at T ms" lines (one per 10 flips). An event = a 10-flip group
whose length exceeds the median of the 30 groups around it by MIN ms or more. Prints per day the events' size by
run-clock range, events per run, and folds the events' wall-clock times (log mtime - time to the log's last flip)
over a scanned period."""
import glob, math, os, re, statistics as S, sys, time, collections
MIN = 200
logs = []
for g in sys.argv[1:]:
    logs += glob.glob(g)
ev = []   # (day, log, t_run_s, excess_ms, wall_s)
runs = collections.Counter()
for p in sorted(set(logs), key=os.path.getmtime):
    T = []
    try:
        for l in open(p, encoding='latin1'):
            if l.startswith('[flip] '):
                m = re.match(r'\[flip\] (\d+) at (\d+) ms', l)
                if m:
                    T.append((int(m.group(1)), int(m.group(2))))
    except OSError:
        continue
    if len(T) < 400 or T[-1][1] < 200000:
        continue
    mt = os.path.getmtime(p)
    day = time.strftime('%m-%d', time.localtime(mt))
    g = [(b[1] - a[1], a[1]) for a, b in zip(T, T[1:]) if b[0] - a[0] == 10]
    runs[day] += 1
    for i, (d, t) in enumerate(g):
        if t < 100000:
            continue
        nb = [x for x, _ in g[max(0, i - 15):i + 16]]
        ex = d - S.median(nb)
        if ex >= MIN:
            ev.append((day, os.path.basename(p), t / 1000.0, ex, mt - (T[-1][1] - t) / 1000.0))
print('runs with flip lines per day:', dict(runs), '| events >= %d ms: %d' % (MIN, len(ev)))
for day in sorted(runs):
    e = [x for x in ev if x[0] == day]
    for lo, hi in ((100, 185), (185, 1000)):
        v = sorted(x[3] for x in e if lo <= x[2] < hi)
        if v:
            print('  %s run clock %3d-%-4d s: %3d events (%.1f per run), size median %4.0f ms, quartiles %4.0f-%4.0f, max %4.0f; share >= 900 ms: %2.0f%%' % (
                day, lo, hi, len(v), len(v) / runs[day], v[len(v) // 2], v[len(v) // 4], v[3 * len(v) // 4], v[-1], 100 * sum(1 for x in v if x >= 900) / len(v)))
# fold today's large events by wall clock and by run clock
last = sorted(runs)[-1]
big = [x for x in ev if x[0] == last and x[3] >= 700 and x[2] >= 185]
print('folding %d events of %s (>= 700 ms, run clock >= 185 s):' % (len(big), last))
def best(vals, label):
    out = []
    P = 27.0
    while P <= 31.0:
        c = sum(math.cos(2 * math.pi * v / P) for v in vals); s = sum(math.sin(2 * math.pi * v / P) for v in vals)
        out.append((math.hypot(c, s) / len(vals), P)); P += 0.005
    out.sort(reverse=True)
    print('  %s: best period %.3f s with concentration R = %.2f (1 = all at one phase; ~%.2f expected by chance); next %.3f (R %.2f)' % (
        label, out[0][1], out[0][0], 1.0 / math.sqrt(len(vals)), out[1][1], out[1][0]))
if len(big) >= 8:
    best([x[4] for x in big], 'wall clock')
    best([x[2] for x in big], 'run clock ')
    # spacing between consecutive events inside a run
    sp = []
    byl = collections.defaultdict(list)
    for x in big:
        byl[x[1]].append(x[2])
    for v in byl.values():
        v.sort(); sp += [b - a for a, b in zip(v, v[1:])]
    sp.sort()
    print('  spacing between consecutive events in a run: median %.1f s, quartiles %.1f-%.1f (n %d)' % (sp[len(sp) // 2], sp[len(sp) // 4], sp[3 * len(sp) // 4], len(sp)))
