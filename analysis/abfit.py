import sys, math
# abfit.py [maxw=<n>] <run log> [<run log> ...]: per run, a per-window least-squares fit of the in-run A/B windows
# (GOW_AB=<lever>,1,<start>; the "[ab]" lines):
#   log(fps) = a + b * on + c * log(draws a frame)
# b = the lever's effect at equal draws. The window sequence is fixed, so in a scripted scene the on and the off
# windows of one run do not hold the same scene, and the plain on/off means carry that imbalance (a null run of a
# fight read +6.7% with 5.2% fewer draws in its on windows). A null run (GOW_AB=none) must give b near 0: check
# that first. Prints the plain on/off figure beside the fitted one, then the pooled b of the runs given.
def rows(path):
    out = []
    for l in open(path, errors='replace'):
        i = l.find('[ab]')
        if i < 0: continue
        d = {}
        for tok in l[i:].split()[1:]:
            if '=' in tok:
                k, v = tok.split('=', 1); d[k] = v
        try:
            out.append(dict(w=int(d['w']), on=int(d['on']), fr=int(d['frames']), secs=float(d['secs']), dr=int(d['draws'])))
        except Exception:
            continue
    return out

def solve3(A, y):
    # normal equations of a 3-column design, by Cramer's rule; returns the coefficients and the lever's s.e.
    n = len(y)
    M = [[sum(A[k][i] * A[k][j] for k in range(n)) for j in range(3)] for i in range(3)]
    v = [sum(A[k][i] * y[k] for k in range(n)) for i in range(3)]
    def det(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    D = det(M)
    beta = []
    for c in range(3):
        m = [row[:] for row in M]
        for r in range(3): m[r][c] = v[r]
        beta.append(det(m) / D)
    res = [y[k] - sum(A[k][i] * beta[i] for i in range(3)) for k in range(n)]
    s2 = sum(e * e for e in res) / (n - 3)
    cof = M[0][0] * M[2][2] - M[0][2] * M[2][0]
    return beta, (s2 * cof / D) ** 0.5

pool = []
maxw = None   # maxw=<n> among the arguments: only windows 0..n (a bench that stops its input before the run ends)
for path in sys.argv[1:]:
    if path.startswith('maxw='):
        maxw = int(path[5:]); continue
    name = path.replace('\\', '/').split('/')[-1]
    r = [x for x in rows(path) if x['secs'] < 1.5 and x['fr'] > 0 and (maxw is None or x['w'] <= maxw)]
    if len(r) < 20:
        print(name, 'NO DATA'); continue
    fps = sorted(x['fr'] / x['secs'] for x in r); med = fps[len(fps) // 2]
    r = [x for x in r if x['fr'] / x['secs'] > 0.5 * med]
    y = [math.log(x['fr'] / x['secs']) for x in r]
    ld = [math.log(x['dr'] / x['fr']) for x in r]
    mld = sum(ld) / len(ld)
    A = [[1.0, float(x['on']), l - mld] for x, l in zip(r, ld)]
    n1 = sum(1 for x in r if x['on']); n0 = len(r) - n1
    if not n1 or not n0:
        print(name, 'no on/off windows'); continue
    plain = sum(yy for yy, x in zip(y, r) if x['on']) / n1 - sum(yy for yy, x in zip(y, r) if not x['on']) / n0
    dimb = sum(l for l, x in zip(ld, r) if x['on']) / n1 - sum(l for l, x in zip(ld, r) if not x['on']) / n0
    (a, b, c), se = solve3(A, y)
    print('%-12s n %d | plain %+.1f%% (draws on/off %+.1f%%) | fitted lever %+.1f%% +- %.1f | fps against draws exponent %.2f'
          % (name, len(r), 100 * (math.exp(plain) - 1), 100 * (math.exp(dimb) - 1), 100 * (math.exp(b) - 1), 100 * se, c))
    pool.append((b, se))
if len(pool) > 1:
    w = [1 / s ** 2 for _, s in pool]
    b = sum(m * k for (m, _), k in zip(pool, w)) / sum(w)
    print('  pooled (%d runs): %+.1f%% +- %.1f' % (len(pool), 100 * (math.exp(b) - 1), 100 / sum(w) ** 0.5))
