"""pad_anchors.py <played log> <pad recording> <out pad>: add anchor lines to a PAD_RECORD file made before
PAD_RECORD wrote them itself. An anchor is `@ <pad seconds> <guest flips>` for each autosave of the session:
the flip count comes from the log ([flip] and [heartbeat] lines around the AutoSave line, interpolated), the pad
time from the recording's own (seconds, flips) pairs. PAD_PLAY re-aligns its clock at each anchor."""
import re, sys

log, pad, out = sys.argv[1:4]
pts = []          # (ms, flips) samples of the log, in order
saves = []        # index into pts after which an autosave happened
for l in open(log, encoding="latin1"):
    m = re.match(r"\[flip\] (\d+) at (\d+) ms", l)
    if m:
        pts.append((int(m.group(2)), int(m.group(1)))); continue
    m = re.match(r"\[heartbeat\] t=(\d+)s flips=(\d+)", l)
    if m:
        pts.append((int(m.group(1)) * 1000, int(m.group(2)))); continue
    if "[cellSaveData] AutoSave(" in l:
        saves.append(len(pts))
lines = [l for l in open(pad) if not l.startswith("@")]
rec = [(float(p[0]), int(p[6])) for p in (l.split() for l in lines) if len(p) >= 7]
anchors = []
for i in saves:
    if i == 0 or i >= len(pts):
        continue
    (m0, f0), (m1, f1) = pts[i - 1], pts[i]
    fl = (f0 + f1) // 2                      # the save sits between the two samples
    # pad time for that flip count: interpolate between the recording's lines around it
    lo = max((r for r in rec if r[1] <= fl), key=lambda r: r[1], default=rec[0])
    hi = min((r for r in rec if r[1] >= fl), key=lambda r: r[1], default=rec[-1])
    t = lo[0] if hi[1] == lo[1] else lo[0] + (hi[0] - lo[0]) * (fl - lo[1]) / (hi[1] - lo[1])
    anchors.append([t, fl, (m0 + m1) / 2000.0, hi[0] - lo[0]])
# Where the recording has no line near the save (a cutscene), its two neighbours say little about the time:
# use the log's time plus the pad clock's offset from it, taken from the anchors that are well bracketed.
tight = sorted(a[0] - a[2] for a in anchors if a[3] <= 2.0)
off = tight[len(tight) // 2] if tight else 0.0
for a in anchors:
    if a[3] > 2.0:
        a[0] = a[2] + off
    print(f"autosave at log {a[2]:.3f} s: flips {a[1]}, pad time {a[0]:.3f} (pad lines {a[3]:.1f} s apart, offset {off:+.3f})")
anchors = [(a[0], a[1]) for a in anchors]
# anchors go before the first line at or after their time
res = []; k = 0
for l in lines:
    p = l.split()
    while k < len(anchors) and len(p) >= 6 and float(p[0]) >= anchors[k][0]:
        res.append(f"@ {anchors[k][0]:.3f} {anchors[k][1]}\n"); k += 1
    res.append(l)
for a in anchors[k:]:
    res.append(f"@ {a[0]:.3f} {a[1]}\n")
open(out, "w", newline="\n").writelines(res)
print(f"{len(anchors)} anchor(s) written to {out}")
