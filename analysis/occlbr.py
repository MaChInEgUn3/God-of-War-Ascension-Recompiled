"""occlbr.py <pass dump dir>: how a title brackets its ZPASS (occlusion) queries, from a pass dump taken with
RSX_ENGINE_PASS_DUMP_DRAWS=1 on a build whose draws.txt carries the report methods (m 17C8 = clear, m 17CC =
enable, m 1800 = get report). A bracket = the draws between a clear of type 1 and the next type-1 report. Prints
the brackets by number of draws, and for the one-draw brackets: the pass's colour target and zeta, whether the draw
writes colour / depth, its depth test, and its index (or vertex) count; the range of report offsets; and per pass
target how many of its draws are test draws."""
import collections, re, statistics as st, sys
d = sys.argv[1]
P = {}
for l in open(d + '/passes.txt', encoding='latin1'):
    m = re.match(r'pass (\d+): draws (\d+) clears (\d+) \| (.*?)\| zeta (\S+)', l)
    if m:
        t = m.group(4).split()
        P[int(m.group(1))] = (t[1] if len(t) > 1 else '?', m.group(5), int(m.group(2)))
since = None; en = None; last = None; cnt = collections.Counter(); rows = []; ms = collections.Counter()
per_pass_draws = collections.Counter(); per_pass_tests = collections.Counter(); en_draws = collections.Counter()
for l in open(d + '/draws.txt', encoding='latin1'):
    m = re.match(r'pass (\d+) draw (\d+): pipe \S+ cmask (\w+) .* dt (\d) dw (\d)', l)
    if m:
        last = {'pass': int(m.group(1)), 'cmask': int(m.group(3), 16), 'dt': m.group(4), 'dw': m.group(5), 'n': 0}
        key = P.get(last['pass'], ('?', '?', 0))[:2]; per_pass_draws[key] += 1
        en_draws['count on' if en else 'count off' if en == 0 else 'unknown'] += 1
        if since is not None: since.append(last)
        continue
    m = re.match(r'\s+after: topo (\d+) verts (\d+) idx (\d+)', l)
    if m and last:
        last['n'] = int(m.group(3)) or int(m.group(2)); continue
    m = re.match(r'\s+m (\w+) = (\w+)', l)
    if not m: continue
    k = m.group(1).upper(); v = int(m.group(2), 16); ms[k] += 1
    if k == '17C8':
        if (v & 3) == 1: since = []
    elif k == '17CC': en = v & 1
    elif k == '1800' and (v >> 24) == 1:
        if since is None: cnt['report with no clear before it'] += 1
        else:
            n = len(since)
            cnt['0 draws' if n == 0 else '1 draw' if n == 1 else '2-16 draws' if n <= 16 else 'over 16 draws'] += 1
            if n == 1:
                rows.append((since[0], v & 0xFFFFFF)); per_pass_tests[P.get(since[0]['pass'], ('?', '?', 0))[:2]] += 1
        since = None
print('methods:', {k: ms[k] for k in ('17C8', '17CC', '1800')}, '| brackets:', dict(cnt), '| draws:', dict(en_draws))
g = collections.Counter(); vs = collections.defaultdict(list)
for dr, off in rows:
    key = P.get(dr['pass'], ('?', '?', 0))[:2] + ('colour off' if dr['cmask'] == 0 else 'colour on', 'dw' + dr['dw'], 'dt' + dr['dt'])
    g[key] += 1; vs[key].append(dr['n'])
for k, v in g.most_common(8):
    s = sorted(vs[k])
    print('  one-draw tests %5d: target %s zeta %s, %s, %s %s | indices median %d, p10 %d, p90 %d' % (
        v, k[0], k[1], k[2], k[3], k[4], st.median(s), s[len(s) // 10], s[len(s) * 9 // 10]))
offs = sorted(set(o for _, o in rows))
if offs: print('report offsets of one-draw tests: %d distinct, 0x%X..0x%X' % (len(offs), offs[0], offs[-1]))
for key, n in per_pass_draws.most_common(6):
    print('  target %s zeta %s: %d draws, %d of them one-draw tests' % (key[0], key[1], n, per_pass_tests[key]))
