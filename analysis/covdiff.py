#!/usr/bin/env python3
"""covdiff.py: set difference of PPU_COV dumps (which guest functions a run entered in a time window).

usage: covdiff.py --a <dump> [<dump> ...] --b <dump> [<dump> ...] [--main] [--top N]

Group A and group B are two kinds of run (for example runs that reach a state and runs that do not). Prints
the functions entered in EVERY run of A and in NO run of B, then the reverse, each ordered by its first entry
(ms from the window's start, the earliest across the group's runs), with the entry counts and the thread mask.
--main keeps only entries made on guest thread 1. A dump line is
"<addr> <entries> <entries on thread 1> <thread mask> <first ms> <last ms>"; see PPU_COV in docs/DEBUGGING.md.
"""
import sys


def load(path, main_only):
    out = {}
    with open(path, encoding="latin-1") as f:
        for ln in f:
            if ln.startswith("#"):
                continue
            p = ln.split()
            if len(p) < 6:
                continue
            n, n1 = int(p[1]), int(p[2])
            if main_only and not n1:
                continue
            out[int(p[0], 16)] = (n1 if main_only else n, int(p[3], 16), int(p[4]), int(p[5]))
    return out


def only(xs, ys, top, title):
    common = set(xs[0])
    for x in xs[1:]:
        common &= set(x)
    for y in ys:
        common -= set(y)
    rows = sorted(common, key=lambda a: min(x[a][2] for x in xs))
    print("%s: %d functions" % (title, len(rows)))
    for a in rows[:top]:
        firsts = [x[a][2] for x in xs]
        counts = [x[a][0] for x in xs]
        mask = 0
        for x in xs:
            mask |= x[a][1]
        tids = ",".join(str(i) for i in range(32) if mask >> i & 1)
        print("  %06X first %6d..%-6d ms  entries %s  tids %s" % (a, min(firsts), max(firsts),
                                                                 "/".join(str(c) for c in counts), tids))
    if len(rows) > top:
        print("  ... %d more (raise --top)" % (len(rows) - top))


def main():
    args = sys.argv[1:]
    groups = {"--a": [], "--b": []}
    main_only, top, cur = False, 60, None
    i = 0
    while i < len(args):
        if args[i] in groups:
            cur = args[i]
        elif args[i] == "--main":
            main_only = True
        elif args[i] == "--top":
            i += 1
            top = int(args[i])
        elif cur:
            groups[cur].append(args[i])
        i += 1
    if not groups["--a"] or not groups["--b"]:
        sys.exit(__doc__)
    a = [load(p, main_only) for p in groups["--a"]]
    b = [load(p, main_only) for p in groups["--b"]]
    print("A: %s functions a run; B: %s" % ("/".join(str(len(x)) for x in a), "/".join(str(len(x)) for x in b)))
    only(a, b, top, "in every A run and in no B run")
    only(b, a, top, "in every B run and in no A run")


if __name__ == "__main__":
    main()
