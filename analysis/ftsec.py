"""ftsec.py <ftrace>: a PS3_FTRACE window second by second: flips, kernel threads inside runs, EDGE runs and their
mean length, the mean length of the other workloads' runs, the walker's share inside FIFO passes, fence waits
(count, ms), the main thread's sleeps (ms) and the longest walker pass that ends in the second."""
import collections, sys
p = sys.argv[1]
t0 = t1 = None
A = collections.defaultdict(lambda: collections.Counter())
def spread(key, s, e):
    x = s
    while x < e:
        k = int((x - t0) // 1e6); nxt = min(e, t0 + (k + 1) * 1e6)
        A[k][key] += nxt - x; x = nxt
for l in open(p, encoding='latin1'):
    if l[0] == '#':
        for tok in l.split():
            if tok.startswith('start_us='): t0 = float(tok.split('=')[1])
            if tok.startswith('end_us='): t1 = float(tok.split('=')[1])
        continue
    f = l.split()
    if len(f) < 6 or f[0] == 'T':
        continue
    s, e = max(float(f[4]), t0), min(float(f[5]), t1)
    if e < s or (e == s and f[0] != '9'):
        continue
    k = min(int((e - t0) // 1e6), int((t1 - t0 - 1) // 1e6))
    if f[0] == '9':
        A[k]['flips'] += 1
    elif f[0] == '4':
        w = int(f[3], 16)
        spread('run', s, e)
        if w in (15, 16, 17):
            A[k]['edge_n'] += 1; A[k]['edge_us'] += float(f[5]) - float(f[4])
        elif w != 8:
            A[k]['oth_n'] += 1; A[k]['oth_us'] += float(f[5]) - float(f[4])
    elif f[0] == '7':
        spread('drain', s, e)
        A[k]['maxdrain'] = max(A[k]['maxdrain'], float(f[5]) - float(f[4]))
    elif f[0] == '12':
        A[k]['fw_n'] += 1; spread('fw', s, e)
    elif f[0] == '2' and f[1] == '1':
        spread('msleep', s, e)
n = int((t1 - t0) // 1e6)
print('sec  flips  threads_in_runs  edge_runs  edge_us  other_us  walker_in_passes  fence_waits  fence_ms  main_sleep_ms  longest_pass_ms')
for k in range(n):
    a = A[k]
    print('%3d  %5d  %15.2f  %9d  %7.1f  %8.1f  %16.2f  %11d  %8.1f  %13.0f  %15.0f' % (
        k, a['flips'], a['run'] / 1e6, a['edge_n'], a['edge_us'] / max(1, a['edge_n']), a['oth_us'] / max(1, a['oth_n']),
        a['drain'] / 1e6, a['fw_n'], a['fw'] / 1000, a['msleep'] / 1000, a['maxdrain'] / 1000))
