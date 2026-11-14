"""ftbins.py <ftrace> [bin s, default 1]: per thread and span kind of a PS3_FTRACE file, the milliseconds spent per
time bin, for the (thread, kind, a) rows that change most across the window. Also flips per bin (kind 9 = FLIP_REQ)
and the longest single spans. Kinds: 1 syscall, 2 usleep, 3 spu wait, 4 spu busy, 5 sema, 6 sema gave up, 7 drain,
8 flip fifo, 9 flip req, 10 present, 11 gpu encode, 12 gpu wait, 13 readback, 14 gpu exec."""
import collections, sys
p = sys.argv[1]
B = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
names, t0, t1 = {}, None, None
rows = collections.defaultdict(lambda: collections.Counter())
cnt = collections.defaultdict(lambda: collections.Counter())
longest = []
for l in open(p, encoding="latin1"):
    if l[0] == "#":
        for tok in l.split():
            if tok.startswith("start_us="):
                t0 = float(tok.split("=")[1])
            if tok.startswith("end_us="):
                t1 = float(tok.split("=")[1])
        continue
    f = l.split()
    if f[0] == "T":
        names[int(f[1])] = " ".join(f[2:])
        continue
    if len(f) < 6:
        continue
    kind, tid, a, b, s, e = f[0], int(f[1]), f[2], f[3], float(f[4]), float(f[5])
    key = (tid, kind, a if kind in ("1", "4", "12") else "")
    # spread the span over the bins it covers
    x = s
    while x < e:
        k = int((x - t0) / (B * 1e6))
        nxt = min(e, t0 + (k + 1) * B * 1e6)
        rows[key][k] += (nxt - x) / 1000.0
        x = nxt
    cnt[key][int((e - t0) / (B * 1e6))] += 1
    if e - s > 60000:
        longest.append((e - s, tid, kind, a, b, s))
nb = int((t1 - t0) / (B * 1e6))
def line(key, v):
    return " ".join("%5.0f" % v.get(k, 0) for k in range(nb))
flips = [k for k in cnt if k[1] == "9"]
for k in flips:
    print("flips/bin  T%d %-22s: %s" % (k[0], names.get(k[0], "?")[:22], " ".join("%5d" % cnt[k].get(i, 0) for i in range(nb))))
out = []
for key, v in rows.items():
    vals = [v.get(k, 0.0) for k in range(nb)]
    if max(vals) - min(vals) < 60 * B:
        continue
    out.append((max(vals) - min(vals), key, v))
print("ms per %.0f s bin, rows that vary by 60 ms or more per second (thread, kind, a):" % B)
for d, key, v in sorted(out, key=lambda r: -r[0])[:22]:
    print("T%-3d %-22s k%-2s a%-5s: %s" % (key[0], names.get(key[0], "?")[:22], key[1], key[2][:5], line(key, v)))
print("spans over 60 ms:")
for d, tid, kind, a, b, s in sorted(longest, reverse=True)[:14]:
    print("  %7.1f ms  T%d %-22s kind %s a %s b %s at %.3f s" % (d / 1000.0, tid, names.get(tid, "?")[:22], kind, a, b, s / 1e6))
