"""abnull.py <log>...: is a scene fit for an in-run A/B? For a run made with GOW_AB=none (no lever), print the
autocorrelation of frames per window and the on/off difference that each pattern would report with nothing
switched: the old periodic pattern (on, off, off, on) and the pattern the log carries. A difference well outside
its own error, or a high autocorrelation at the pattern's period, means the scene's own cycle aliases with the
pattern and a lever's "effect" there is not one."""
import re, statistics as S, sys
for p in sys.argv[1:]:
    rows = [dict(re.findall(r'(\w+)=([\w.]+)', l)) for l in open(p, encoding='latin1') if l.startswith('[ab]')][2:]
    rows = [r for r in rows if float(r['frames']) > 0]
    f = [float(r['frames']) for r in rows]
    w = [int(r['w']) for r in rows]
    m, v = S.mean(f), S.pvariance(f)
    ac = lambda k: sum((f[i] - m) * (f[i + k] - m) for i in range(len(f) - k)) / (len(f) - k) / v if v else 0.0
    def diff(sel):
        a = [x for x, s in zip(f, sel) if s]; b = [x for x, s in zip(f, sel) if not s]
        se = (S.pvariance(a) / len(a) + S.pvariance(b) / len(b)) ** 0.5
        return '%+.1f%% +- %.1f%%' % (100 * (S.mean(a) / S.mean(b) - 1), 100 * se / S.mean(b))
    print('%s: lever %s, %d windows of %s s, frames %.2f sd %.2f' % (p, rows[0].get('lever'), len(f), rows[0].get('secs'), m, v ** 0.5))
    print('  autocorrelation: ' + ' '.join('lag%d %.2f' % (k, ac(k)) for k in (1, 2, 3, 4, 5, 6, 8, 12, 30)))
    print('  on/off with nothing switched: periodic pattern %s | logged pattern %s' % (
        diff([not (((x + 1) >> 1) & 1) for x in w]), diff([r['on'] == '1' for r in rows])))
