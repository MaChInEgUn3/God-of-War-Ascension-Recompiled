"""spu_regpromote.py <spu_recomp.c> <out.c> <func_hex|all> [...]: keep SPU registers in C locals.

Lifted SPU code reads and writes every register through ctx->gpr[n]. Each local-store store goes through a
uint8_t pointer into the same context, which may alias any of them, so the compiler reloads registers from
memory after every store and spills on every write. This rewrites chosen lifted functions so that registers
live in locals (u128 r_<n>) between "barriers", which the compiler can keep in vector registers:

- non-barrier lines: ctx->gpr[n] becomes r_<n>. A line is a non-barrier only if every ctx use on it is
  ctx->gpr[n] or the ctx argument of spu_ls_read128/spu_ls_write128 (those touch ctx->ls only, with the LS
  probes compiled out, as the port builds lifted code);
- barrier lines (calls into other lifted functions, spu_drain_call, halts, returns, any other ctx use) are
  left as they were, with every used register stored to ctx->gpr before the line and loaded back after it;
- the function's registers are loaded at entry and stored before its closing brace.

Same operations in the same order on the same values: the output is bit-exact by construction. The replay
gate (SPU_JOB_REPLAY) checks it anyway. Functions it cannot rewrite safely (an indexed register, a barrier
under a braceless if) are left unchanged and reported.

Result (2026-10-25, GoW:A EDGE module d935, docs/EDGE_NATIVE.md): bit-exact (400/400 replayed jobs), but no
real gain. E550 alone or all 1027 functions: at most ~5% on replay time, inside the noise of those runs. SPU
code keeps ~100 registers live, and x86-64-v3 has 16 vector registers, so the locals spill to the stack
instead. E550 grew from 13.5k to 22.9k x86 instructions, with 3.6x the stack accesses. Kept as a record; not used
in the build.
"""
import re
import sys

GPR = re.compile(r"ctx->gpr\[(\d+)\]")
HDR = re.compile(r"^void (\w+_spu_func_([0-9A-F]{8}))\(spu_context\* ctx\) \{\s*$")
SAFE_CALL = re.compile(r"\bspu_ls_(?:read|write)128\(ctx,")


def is_barrier(line):
    s = GPR.sub("", line)
    s = SAFE_CALL.sub("(", s)
    return re.search(r"\bctx\b", s) is not None or re.search(r"\breturn\b|SPU_RET", s) is not None


def promote(body):
    """body: lines after the header up to and including the closing brace. Returns new lines or a reason."""
    if any(re.search(r"ctx->gpr\[[^0-9]", l) for l in body):
        return "indexed register"
    regs = sorted({int(n) for l in body for n in GPR.findall(l)})
    if not regs:
        return "no registers"
    close = max(i for i, l in enumerate(body) if l.strip() == "}")
    flush = "        " + " ".join(f"ctx->gpr[{n}] = r_{n};" for n in regs)
    load = "        " + " ".join(f"r_{n} = ctx->gpr[{n}];" for n in regs)
    out = ["        u128 " + ", ".join(f"r_{n} = ctx->gpr[{n}]" for n in regs) + ";"]
    prev = ""
    for i, l in enumerate(body):
        if i == close:
            out += [flush, l] + body[i + 1:]
            return out
        if is_barrier(l):
            p = prev.rstrip()
            if re.match(r"\s*(if|else|for|while)\b", prev) and not p.endswith((";", "{", "}")):
                return "barrier under a braceless if/loop"
            out += [flush, l, load]
        else:
            out.append(GPR.sub(lambda m: f"r_{m.group(1)}", l))
        if l.strip():
            prev = l
    return "no closing brace"


def main():
    src, dst, want = sys.argv[1], sys.argv[2], {a.upper() if a.lower() == "all" else a.upper().rjust(8, "0") for a in sys.argv[3:]}
    lines = open(src, newline="").read().split("\n")
    out, i, done, skipped = [], 0, [], []
    while i < len(lines):
        m = HDR.match(lines[i].rstrip("\r"))
        if not m or not ("ALL" in want or m.group(2) in want):
            out.append(lines[i]); i += 1; continue
        j = i + 1
        while j < len(lines) and not lines[j].startswith("void ") and not lines[j].startswith("static "):
            j += 1
        cr = lines[i].endswith("\r")
        body = [l.rstrip("\r") for l in lines[i + 1:j]]
        r = promote(body)
        if isinstance(r, str):
            skipped.append((m.group(2), r)); out += lines[i:j]
        else:
            done.append(m.group(2)); out.append(lines[i]); out += [l + ("\r" if cr else "") for l in r]
        i = j
    open(dst, "w", newline="").write("\n".join(out))
    print(f"promoted {len(done)}: {' '.join(done[:20])}{' ...' if len(done) > 20 else ''}")
    for a, why in skipped:
        print(f"skipped {a}: {why}")


if __name__ == "__main__":
    main()
