"""Pilot of 'registers in locals' on lifted functions (measurement only, never built into the game).
usage: python pilot.py <spu_recomp.c> <fn,fn,...> <out.c>
Every ctx->gpr[N] in a function becomes a local r_N, loaded at entry; the registers the function writes are
stored back before SPU_RET / SPU_DIRECT / spu_stop / spu_rdch / spu_wrch / spu_halt and before each nested
lifted call, and every local is reloaded after a nested call (the callee may change any register)."""
import re
import sys

src, fns, out = sys.argv[1:4]
L = open(src, encoding='utf8').read().split('\n')
gre = re.compile(r'ctx->gpr\[(\d+)\]')
wre = re.compile(r'ctx->gpr\[(\d+)\] = ')
for fn in fns.split(','):
    head = next(i for i, l in enumerate(L) if l.startswith('void %s(spu_context* ctx) {' % fn))
    end = next(i for i in range(head + 1, len(L)) if L[i].startswith('}'))
    body = L[head + 1:end]
    used = sorted({int(m) for l in body for m in gre.findall(l)})
    written = sorted({int(m) for l in body for m in wre.findall(l)})
    FL = '{ ' + ' '.join('ctx->gpr[%d] = r_%d;' % (n, n) for n in written) + ' } '
    reload_ = ' '.join('r_%d = ctx->gpr[%d];' % (n, n) for n in used)
    nb = ['    u128 ' + ', '.join('r_%d = ctx->gpr[%d]' % (n, n) for n in used) + ';']
    calls = 0
    for l in body:
        t = gre.sub(lambda m: 'r_' + m.group(1), l)
        if 'SPU_RET(' in t:
            t = t.replace('SPU_RET(', FL + 'SPU_RET(')
        elif 'SPU_DIRECT(' in t:
            t = t.replace('SPU_DIRECT(', FL + 'SPU_DIRECT(', 1)
        elif re.search(r'spu_stop\(|spu_rdch\(|spu_wrch\(|spu_halt\(', t):
            t = '    ' + FL + t.lstrip()
        elif '_spu_func_' in t:
            calls += 1
            assert '{ int32_t _si' in t, t
            t = t.replace('{ int32_t _si', FL + '{ int32_t _si', 1) + ' ' + reload_
        elif re.search(r'\bspu_(ind|drain|img_restore|link_ret)', t) or 'return' in t:
            raise SystemExit('unhandled flush point: ' + t.strip()[:160])
        nb.append(t)
    L[head + 1:end] = nb
    print(fn, 'used', len(used), 'written', len(written), 'nested calls', calls)
open(out, 'w', encoding='utf8').write('\n'.join(L))
