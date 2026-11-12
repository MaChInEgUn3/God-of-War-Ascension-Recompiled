"""pad_mash.py <pad in> <pad out> <from s> <to s> <buttons hex> [period s]: add presses of a button to a
PAD_RECORD file between two times where the recording has no line (press for half a period, release for half),
keeping the sticks of the line before. For a replay whose prompt may come up later than it did in the recording:
the person's last press may fall before it."""
import sys
src, out, t0, t1, b = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5], 16)
per = float(sys.argv[6]) if len(sys.argv) > 6 else 0.4
lines = open(src).read().splitlines()
res = []; done = False; prev = None
for l in lines:
    p = l.split()
    if not l.startswith("@") and len(p) >= 6:
        t = float(p[0])
        assert not (t0 < t < t1), f"the recording has a line at {t} inside the range"
        if t >= t1 and not done and prev:
            k = 0
            while t0 + k * per + per / 2 < t1:
                res.append(f"{t0 + k * per:.3f} {b:04X} {' '.join(prev[2:])}")
                res.append(f"{t0 + k * per + per / 2:.3f} 0000 {' '.join(prev[2:])}")
                k += 1
            done = True
            print(f"{k} presses of {b:04X} added from {t0} to {t1} s")
        prev = p
    res.append(l)
open(out, "w", newline="\n").write("\n".join(res) + "\n")
