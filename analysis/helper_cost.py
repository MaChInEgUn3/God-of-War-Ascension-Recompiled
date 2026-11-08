"""Host instructions per inline helper in one function, from a clang /FA listing with DWARF .loc lines.
usage: python helper_cost.py <listing.asm> <function symbol> <source .c/.cpp>
Compile with /clang:-gdwarf /FA (not /Z7: CodeView has no .loc). Each instruction is charged to the innermost
source line its .loc names; a header line is mapped to the inline function whose body holds it. Lines in the
given source are charged to "lifted glue". Used for the SPU and PPU codegen notes in docs/LIFTER_CODEGEN.md."""
import collections
import re
import sys

asm, fn, csrc = sys.argv[1:4]
files = {}
lines = open(asm, encoding='utf8', errors='replace').read().split('\n')
for l in lines:
    m = re.match(r'\s*\.file\s+(\d+)\s+"([^"]*)"(?:\s+"([^"]*)")?', l)
    if m:
        d, f = m.group(2), m.group(3)
        files[int(m.group(1))] = (d.replace('\\\\', '/') + '/' + f) if f else d.replace('\\\\', '/')
start = next(i for i, l in enumerate(lines) if l.startswith(fn + ':'))
body = []
for l in lines[start + 1:]:
    if l.strip().startswith('.seh_endproc') or re.match(r'^\w+:\s*(#.*)?$', l) and not l.startswith(('.L', '$')) and fn not in l:
        break
    body.append(l)


def func_map(path):
    """line -> enclosing inline function name for a header"""
    try:
        src = open(path, encoding='utf8', errors='replace').read().split('\n')
    except OSError:
        return {}
    out = {}
    cur = None
    depth = 0
    for i, l in enumerate(src, 1):
        if depth == 0:
            m = re.match(r'^(?:static\s+)?(?:inline\s+|SPU__X8_SLOW\s+|__attribute__\(\([^)]*\)\)\s+)*[\w\s\*]+?\b(\w+)\s*\([^;]*$', l)
            if m and not l.lstrip().startswith(('#', '//', '/*', '*')):
                cur = m.group(1)
        depth += l.count('{') - l.count('}')
        out[i] = cur if depth > 0 or '}' in l else None
    return out


fmaps = {}
cost = collections.Counter()
cur = 'prologue'
n = 0
for l in body:
    m = re.match(r'\s*\.loc\s+(\d+)\s+(\d+)', l)
    if m:
        fid, ln = int(m.group(1)), int(m.group(2))
        path = files.get(fid, '?')
        if path.endswith(csrc.split('/')[-1]) or fid == 1:
            cur = 'lifted glue'
        else:
            if path not in fmaps:
                fmaps[path] = func_map(path)
            cur = '%s:%s' % (path.split('/')[-1], fmaps[path].get(ln) or ln)
        continue
    s = l.strip()
    if not s or s.startswith(('.', '#')) or s.endswith(':'):
        continue
    cost[cur] += 1
    n += 1
print('instructions', n)
for k, v in cost.most_common(30):
    print('%6d %5.1f%%  %s' % (v, 100 * v / n, k))
