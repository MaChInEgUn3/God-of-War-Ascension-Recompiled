"""framediff.py <frames dir A> <frames dir B> [last n, default 8]: compare the frame dumps two runs wrote at the
same present index (f_<index>.ppm), for the last n indices both have: mean absolute difference per channel and
the share of pixels where any channel differs by more than 24. For a scene that holds still, two runs of the same
build give the noise floor (flames, particles)."""
import os, sys

def load(p):
    d = open(p, "rb").read(); h = d.split(b"\n", 3); w, hh = map(int, h[1].split())
    return w, hh, h[3]

a, b = sys.argv[1], sys.argv[2]; n = int(sys.argv[3]) if len(sys.argv) > 3 else 8
common = sorted(set(os.listdir(a)) & set(os.listdir(b)))[-n:]
res = []
for f in common:
    wa, ha, pa = load(os.path.join(a, f)); wb, hb, pb = load(os.path.join(b, f))
    if (wa, ha) != (wb, hb):
        print(f, "sizes differ"); continue
    step = 3 * 4                      # every 4th pixel
    tot = 0; big = 0; cnt = 0
    for i in range(0, min(len(pa), len(pb)) - 2, step):
        d0 = abs(pa[i] - pb[i]); d1 = abs(pa[i + 1] - pb[i + 1]); d2 = abs(pa[i + 2] - pb[i + 2])
        tot += d0 + d1 + d2; cnt += 1
        if d0 > 24 or d1 > 24 or d2 > 24:
            big += 1
    res.append((f, tot / (3 * cnt), 100.0 * big / cnt))
for f, m, s in res:
    print(f"  {f}: mean abs diff {m:.2f}, pixels over 24: {s:.2f}%")
if res:
    print(f"{a} vs {b}: {len(res)} frames, mean abs diff {sum(r[1] for r in res) / len(res):.2f}, "
          f"pixels over 24: {sum(r[2] for r in res) / len(res):.2f}% (worst {max(r[2] for r in res):.2f}%)")
