"""Apply the port's hooks to freshly lifted PPU code (idempotent).

The lifter emits every guest function as plain C++ with a ppu_trace_enter()
call first. A port hook that has to run *instead of* a guest function body
(or veto it) is inserted right after that call; this script does it so a
re-lift keeps the hooks without hand edits.

Usage: python patch_lift.py <recompiled dir>
"""
import glob
import os
import sys

# guest function -> C call returning nonzero to skip the body (see gow_vram_guard.c)
HOOKS = {
    0x00370E48: ('gow_release_veto', 'int gow_release_veto(uint32_t obj);',
                 'if (gow_release_veto((uint32_t)ctx->gpr[4])) return;'),
}


def _stv(mn, vs, ra, rb):
    """The current lifter's code for the Cell unaligned vector stores.
    stvlx: the leading 16-sh bytes of vS go to EA..the end of its quadword.
    stvrx: the trailing sh bytes of vS go to the start of EA's quadword."""
    ea = f'(ctx->gpr[{ra}] + ctx->gpr[{rb}])' if ra else f'(ctx->gpr[{rb}])'
    if mn.startswith('stvl'):
        loop = 'for (uint32_t i = 0; i < 16u - sh; i++) m[sh + i] = s[i];'
    else:
        loop = 'for (uint32_t i = 16u - sh; i < 16u; i++) m[i - (16u - sh)] = s[i];'
    return (f'{{ uint64_t ea = {ea}; uint32_t sh = (uint32_t)(ea & 0xF); '
            f'uint8_t* m = vm_base + (uint32_t)(ea & ~0xFULL); '
            f'const uint8_t* s = (const uint8_t*)&ctx->vr[{vs}]; {loop} }} /* {mn} v{vs}, r{ra}, r{rb} */')


def fill_todo_insns(d):
    """An older lifter left a few real instructions as '/* TODO: <insn> */;' no-ops:
    stvlx/stvrx (Cell unaligned vector stores) and vlogefp. A dropped stvlx leaves the
    callee reading an unwritten stack slot (GoW:A: a query in func_002E9270 then returned
    a null object, the game walked it and froze a minute into a new game's first level).
    Fill them in with what the current lifter emits. A lift made by the current lifter has
    none of these lines, so this is a no-op there."""
    import re
    stv = re.compile(r'/\* TODO: (stvlxl?|stvrxl?) v(\d+), r(\d+), r(\d+) \*/;')
    vlog = re.compile(r'/\* TODO: vlogefp v(\d+), v\d+, v(\d+) \*/;')
    total = 0
    for path in sorted(glob.glob(os.path.join(d, 'ppu_recomp_*.cpp'))):
        with open(path, 'rb') as f:
            src = f.read().decode('latin-1')
        n = [0]

        def r_stv(m):
            n[0] += 1
            return _stv(m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)))

        def r_vlog(m):
            n[0] += 1
            return (f'{{ float b[4],d[4]; ppu_vldf4(&ctx->vr[{m.group(2)}],b); for(int i=0;i<4;i++) d[i]=log2f(b[i]); '
                    f'ppu_vstf4(&ctx->vr[{m.group(1)}],d); }} /* vlogefp v{m.group(1)}, v{m.group(2)} */')

        out = vlog.sub(r_vlog, stv.sub(r_stv, src))
        if n[0]:
            with open(path, 'wb') as f:
                f.write(out.encode('latin-1'))
            print(f'{os.path.basename(path)}: filled {n[0]} stvlx/stvrx/vlogefp')
            total += n[0]
    if not total:
        print('no TODO stvlx/stvrx/vlogefp lines (already filled, or a current lift)')


def main(d):
    fill_todo_insns(d)
    for addr, (name, decl, stmt) in HOOKS.items():
        enter = f'ppu_trace_enter(0x{addr:08X}u, ctx);'
        head = f'void func_{addr:08X}(ppu_context* ctx) {{'
        done = False
        for path in sorted(glob.glob(os.path.join(d, 'ppu_recomp_*.cpp'))):
            with open(path, 'rb') as f:
                src = f.read().decode('latin-1')
            i = src.find(head)
            if i < 0:
                continue
            if name in src[i:i + 4000]:
                print(f'{os.path.basename(path)}: func_{addr:08X} already hooked')
                done = True
                continue
            j = src.find(enter, i)
            if j < 0 or j - i > 4000:
                sys.exit(f'func_{addr:08X}: no ppu_trace_enter in its prologue')
            j += len(enter)
            src = src[:i] + f'extern "C" {decl}\n' + src[i:j] + '\n        ' + stmt + src[j:]
            with open(path, 'wb') as f:
                f.write(src.encode('latin-1'))
            print(f'{os.path.basename(path)}: func_{addr:08X} -> {name}')
            done = True
        if not done:
            sys.exit(f'func_{addr:08X} not found')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'recompiled_v015')
