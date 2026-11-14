"""abphase.py <log>...: on/off of an in-run A/B probe run with 1 s windows, read apart for normal seconds and for
seconds inside a dip of the frame rate (median of frames over +-4 windows below DIP, default 16.5)."""
import re, statistics as S, sys
DIP = 16.5
for p in sys.argv[1:]:
    rows = [dict(re.findall(r'(\w+)=([\w.]+)', l)) for l in open(p, encoding='latin1') if l.startswith('[ab]')][2:]
    f = [float(r['frames']) for r in rows]; on = [r['on'] == '1' for r in rows]
    dip = [S.median(f[max(0, i - 4):i + 5]) < DIP for i in range(len(f))]
    out = [p + ':']
    for name, want in (('normal', False), ('dip', True)):
        a = [f[i] for i in range(len(f)) if dip[i] == want and on[i]]
        b = [f[i] for i in range(len(f)) if dip[i] == want and not on[i]]
        if len(a) > 2 and len(b) > 2:
            se = (S.pvariance(a) / len(a) + S.pvariance(b) / len(b)) ** 0.5
            out.append('%s on %.2f (n %d) off %.2f (n %d) %+.1f%% +- %.1f%%' % (name, S.mean(a), len(a), S.mean(b), len(b),
                       100 * (S.mean(a) / S.mean(b) - 1), 100 * se / S.mean(b)))
    r = [x for i, x in enumerate(rows) if not dip[i] and not on[i]]
    if r:
        out.append('off-normal rsx_busy %.2f us_draw %.1f us_run %.1f' % (S.mean(float(x['rsx_busy']) for x in r),
                   S.mean(float(x['us_draw']) for x in r), S.mean(float(x['us_run']) for x in r)))
    print(' | '.join(out))
