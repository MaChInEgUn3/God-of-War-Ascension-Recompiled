"""Codegen density over every lifted SPU module: host instructions per SPU op, stack and ctx share, per function.
usage: python cgdist.py <objdump> <spu src root> <obj root> <out.tsv>"""
import os
import re
import subprocess
import sys
import statistics as st

OBJDUMP, SRC, OBJ, OUT = sys.argv[1:5]
fn_re = re.compile(r'^void (\w+_spu_func_[0-9A-Fa-f]+)\(spu_context\* ctx\) \{')
op_re = re.compile(r'^\s+(ctx->gpr\[\d+\] = |spu_ls_write|spu_wrch|spu_stop|if \(|goto |SPU_DIRECT|spu_ind|ctx->gpr\[0\] = spu_link)')
ins_re = re.compile(r'^\s+[0-9a-f]+:\s+(.*)')
sym_re = re.compile(r'^[0-9a-f]+ <(.+)>:$')
rows = []
for mod in sorted(os.listdir(SRC)):
    c = os.path.join(SRC, mod, 'spu_recomp.c')
    o = os.path.join(OBJ, mod, 'spu_recomp.c.obj')
    if not (os.path.isfile(c) and os.path.isfile(o)):
        continue
    ops = {}
    cur = None
    for l in open(c, encoding='utf8', errors='replace'):
        m = fn_re.match(l)
        if m:
            cur = m.group(1)
            ops[cur] = 0
            continue
        if cur and l.startswith('}'):
            cur = None
            continue
        if cur and op_re.match(l):
            ops[cur] += 1
    p = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', o], capture_output=True, text=True, errors='replace')
    cur = None
    st_ = {}
    for l in p.stdout.splitlines():
        m = sym_re.match(l)
        if m:
            cur = m.group(1)
            st_[cur] = [0, 0, 0, 0]
            continue
        m = ins_re.match(l) if cur else None
        if not m:
            continue
        ins = m.group(1)
        s = st_[cur]
        s[0] += 1
        if '(%rsp)' in ins or '(%rbp)' in ins:
            s[1] += 1
        if 'call' in ins.split()[0] if ins.split() else False:
            s[3] += 1
    for f, n in ops.items():
        if f in st_ and n > 0:
            h, sk, _, cl = st_[f]
            rows.append((mod, f, n, h, sk, cl))
with open(OUT, 'w') as w:
    for r in rows:
        w.write('\t'.join(map(str, r)) + '\n')
big = [r for r in rows if r[2] >= 50]
print('modules', len(set(r[0] for r in rows)), 'functions', len(rows), 'with >=50 SPU ops', len(big))
for name, sel in (('all', rows), ('>=50 ops', big)):
    ratio = [r[3] / r[2] for r in sel]
    stk = [r[4] / r[3] for r in sel if r[3]]
    q = st.quantiles(ratio, n=10)
    print(name, 'host/op median %.1f p10 %.1f p90 %.1f; stack share median %.2f p10 %.2f p90 %.2f' % (
        st.median(ratio), q[0], q[-1], st.median(stk), st.quantiles(stk, n=10)[0], st.quantiles(stk, n=10)[-1]))
    tot_o = sum(r[2] for r in sel)
    tot_h = sum(r[3] for r in sel)
    print('  op-weighted host/op %.1f, stack share %.2f, calls per 100 ops %.1f' % (
        tot_h / tot_o, sum(r[4] for r in sel) / tot_h, 100 * sum(r[5] for r in sel) / tot_o))
for r in rows:
    if r[1].endswith('spu_func_00015400') and '5bc2fdee17f4' in r[0]:
        print('hot fn', r, 'host/op %.1f' % (r[3] / r[2]))
