"""abwait.py <log>...: on/off of an in-run A/B (1 s windows) read by the fence-wait columns of the [ab] line:
ms per frame in each kind of wait, their sum, frames per window and walker ms per frame, with standard errors."""
import re, statistics as S, sys
for p in sys.argv[1:]:
    txt = open(p, encoding='latin1').read()
    rows = [dict(re.findall(r'(\w+)=([\w.]+)', l)) for l in txt.split('\n') if l.startswith('[ab]')][2:]
    rows = [r for r in rows if float(r['frames']) > 0]
    print('%s: %d windows, lever %s' % (p, len(rows), rows[0].get('lever')))
    fr = lambda r: float(r['frames'])
    def show(name, f):
        a = [f(r) for r in rows if r['on'] == '1']; b = [f(r) for r in rows if r['on'] == '0']
        se = (S.pvariance(a) / len(a) + S.pvariance(b) / len(b)) ** 0.5
        print('  %-20s on %7.2f (med %6.2f)  off %7.2f (med %6.2f)  diff %+6.2f +- %.2f' % (
            name, S.mean(a), S.median(a), S.mean(b), S.median(b), S.mean(a) - S.mean(b), se))
    show('frames per window', fr)
    for k in ('w_sync', 'w_slot', 'w_idle', 'w_present'):
        show(k + ' ms/frame', lambda r, k=k: float(r[k]) / fr(r))
    show('all waits ms/frame', lambda r: sum(float(r[k]) for k in ('w_sync', 'w_slot', 'w_idle', 'w_present')) / fr(r))
    show('walker ms/frame', lambda r: float(r['rsx_busy']) * 1000 * float(r['secs']) / fr(r))
    wins = re.split(r'\n(?=\[vk-draw-prof\] \d+ ms:)', txt)[1:]
    for wn in wins[-3:-2]:
        for l in wn.split('\n')[1:8]:
            if 'image destroys' in l:
                print('    ' + l[18:140])
