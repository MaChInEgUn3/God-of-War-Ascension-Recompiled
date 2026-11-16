"""ftwait.py <ftrace> [obj=wid ...]: what the SPURS kernel threads did while the main thread stood in a guest
wait loop. Needs a PS3_FTRACE taken with PPU_TRACE_ADDRS=466e38,467b18 (kind 15 instants: who = tid, a = function,
b = r3). A wait interval = from a func_00466E38 entry of tid 1 with r3 = <obj> to the next kind-15 instant of
tid 1. Per object: intervals, mean / p90 length; the waited workload's runs and run time inside; delay from the
start to its first run and from its last run's end to the end of the wait; mean kernel threads busy; busy time by
workload; kernel nap time (kind 3, a = 0x51EE) summed over threads. Default objects: 81664824=14 816327A4=13."""
import bisect, collections, sys
p = sys.argv[1]
objs = dict((int(a.split('=')[0], 16), int(a.split('=')[1])) for a in sys.argv[2:]) or {0x81664824: 14, 0x816327A4: 13}
runs = []; naps = []; inst = []
for l in open(p, encoding='latin1'):
    if l[0] in '#T': continue
    f = l.split()
    if len(f) < 6: continue
    a, b = float(f[4]), float(f[5])
    if f[0] == '4': runs.append((a, b if b > a else a + b, int(f[1]), int(f[3], 16)))
    elif f[0] == '3' and f[2] == '51ee': naps.append((a, b if b > a else a + b))
    elif f[0] == '15' and f[1] == '1': inst.append((a, int(f[2], 16), int(f[3], 16)))
runs.sort(); naps.sort(); inst.sort()
rs = [r[0] for r in runs]; ns = [n[0] for n in naps]
print('runs %d, naps %d, main-thread instants %d' % (len(runs), len(naps), len(inst)))
def inside(lst, starts, t0, t1, back):
    i = bisect.bisect_left(starts, t0 - back)
    while i < len(lst) and lst[i][0] < t1:
        if lst[i][1] > t0: yield lst[i]
        i += 1
for obj, wid in objs.items():
    L = []; own_n = []; own_t = []; first = []; last = []; busy = collections.Counter(); nap = []; allb = []
    for k in range(len(inst) - 1):
        t0, fn, r3 = inst[k]
        if fn != 0x466E38 or r3 != obj: continue
        t1 = inst[k + 1][0]
        if t1 - t0 > 200000: continue
        L.append(t1 - t0); n = 0; tt = 0.0; f0 = None; l1 = None; tot = 0.0
        for a, b, th, w in inside(runs, rs, t0, t1, 50000):
            d = min(b, t1) - max(a, t0); busy[w] += d; tot += d
            if w == wid:
                n += 1; tt += d
                if f0 is None: f0 = max(a, t0) - t0
                l1 = t1 - min(b, t1)
        own_n.append(n); own_t.append(tt); allb.append(tot)
        if f0 is not None: first.append(f0); last.append(l1)
        nap.append(sum(min(b, t1) - max(a, t0) for a, b in inside(naps, ns, t0, t1, 50000)))
    if not L: print('%08X: no intervals' % obj); continue
    N = len(L); m = lambda v: sum(v) / max(1, len(v)) / 1000.0
    print('%08X (wid %d): %d waits, mean %.2f ms, p90 %.2f | its runs inside: %.1f, run time %.2f ms | first run after %.2f ms, '
          'last run ends %.2f ms before the wait ends' % (obj, wid, N, m(L), sorted(L)[int(N * .9)] / 1000, sum(own_n) / N,
                                                          m(own_t), m(first), m(last)))
    print('   kernel threads busy on average %.2f | nap time summed over threads %.2f ms per wait | busy ms per wait by wid: %s' % (
        sum(allb) / sum(L), m(nap), ' '.join('%d:%.2f' % (w, v / N / 1000) for w, v in busy.most_common(9))))
