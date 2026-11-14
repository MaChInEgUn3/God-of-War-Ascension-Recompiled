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
    # a handler of the level loader's table; its object is the word at 0x0096DF44 (see gow_vram_guard.c)
    0x00370B48: ('gow_loaded_note', 'int gow_loaded_note(uint32_t r3, uint32_t r4, uint32_t r5_unused);',
                 'gow_loaded_note((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5]);'),
    # probes: is this function reached at all (counts printed with GOW_RELEASE_STATS)
    0x0037BD48: ('gow_probe_count', 'int gow_probe_count(int which, uint32_t r3);',
                 'gow_probe_count(0, (uint32_t)ctx->gpr[3]);'),
    0x00328AFC: ('gow_probe_count', 'int gow_probe_count(int which, uint32_t r3);',
                 'gow_probe_count(1, (uint32_t)ctx->gpr[3]);'),
    0x003CA8EC: ('gow_probe_count', 'int gow_probe_count(int which, uint32_t r3);',
                 'gow_probe_count(2, (uint32_t)ctx->gpr[3]);'),
    0x003C9644: ('gow_probe_count', 'int gow_probe_count(int which, uint32_t r3);',
                 'gow_probe_count(3, (uint32_t)ctx->gpr[3]);'),
    0x003C8F64: ('gow_probe_count', 'int gow_probe_count(int which, uint32_t r3);',
                 'gow_probe_count(4, (uint32_t)ctx->gpr[3]);'),
}

# guest label -> statement inserted right after it, in every lifted copy that has the label. The two labels are
# where func_003A7B3C writes a texture's side flags 0x0200 and 0x4000 from a float comparison (r26 / r24 = side
# entry, r3 / r4 = the result, fpr2 = the value, fpr1 = the threshold).
LABEL_HOOKS = {
    'loc_003A8904:': ('gow_want_note', 'int gow_want_note(int site, uint32_t side, uint32_t want, double val, double thr);',
                      'gow_want_note(0, (uint32_t)ctx->gpr[26], (uint32_t)ctx->gpr[3], ctx->fpr[2], ctx->fpr[1]);'),
    'loc_003A8A44:': ('gow_want_note', 'int gow_want_note(int site, uint32_t side, uint32_t want, double val, double thr);',
                      'gow_want_note(1, (uint32_t)ctx->gpr[24], (uint32_t)ctx->gpr[4], ctx->fpr[2], ctx->fpr[1]);'),
}


def label_hooks(d):
    for label, (name, decl, stmt) in LABEL_HOOKS.items():
        found = 0
        for path in sorted(glob.glob(os.path.join(d, 'ppu_recomp_*.cpp'))):
            with open(path, 'rb') as f:
                src = f.read().decode('latin-1')
            pos, changed = 0, False
            while True:
                i = src.find(label, pos)
                if i < 0:
                    break
                found += 1
                e = i + len(label)
                if name + '(' in src[e:e + 200]:
                    print(f'{os.path.basename(path)}: {label} already hooked')
                    pos = e
                    continue
                h = src.rfind('\nvoid func_', 0, i) + 1
                ext = f'extern "C" {decl}\n'
                ins = '\n        ' + stmt
                src = src[:h] + ext + src[h:i] + label + ins + src[e:]
                pos = e + len(ext) + len(ins)
                changed = True
                print(f'{os.path.basename(path)}: {label} -> {name}')
            if changed:
                with open(path, 'wb') as f:
                    f.write(src.encode('latin-1'))
        if not found:
            sys.exit(f'{label} not found')


def main(d):
    label_hooks(d)
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
