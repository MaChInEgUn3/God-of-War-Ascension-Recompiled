"""engstats.py <log>...: totals of the [eng-stats] lines of a run (ENG_PHASE_STATS=<s>, with ENG_SLOW_MS=<ms> for the
'ends over' line): time in draw ends by phase over the whole run, and the same for the ends over the threshold.
Unlike the [eng-slow] lines, which stop after the first 64, these count every draw end. Also the [spirv-time]
total and the slow vkQueueSubmit calls (VK_SLOW_MS)."""
import re, sys

for p in sys.argv[1:]:
    tot = dict.fromkeys(["fetch", "pipe", "tex", "idxbuild", "be", "rest"], 0.0)
    slow = dict.fromkeys(tot, 0.0); slow_n = 0; slow_ms = 0.0; thr = "?"; wall = 0.0; mx = 0.0
    spv = ""; sub_n = 0; sub_ms = 0.0
    for l in open(p, encoding="latin1"):
        if l.startswith("[eng-stats]"):
            m = re.match(r"\[eng-stats\] (\d+) ms: .*\| fetch (\d+) pipe (\d+) tex (\d+) idxbuild (\d+) be (\d+) rest (\d+) \| max ([\d.]+)", l)
            if m:
                wall += float(m.group(1)); mx = max(mx, float(m.group(8)))
                for k, v in zip(tot, m.groups()[1:7]):
                    tot[k] += float(v)
                continue
            m = re.match(r"\[eng-stats\]   ends over (\d+) ms: (\d+), (\d+) ms \| fetch (\d+) pipe (\d+) tex (\d+) idxbuild (\d+) be (\d+) rest (\d+)", l)
            if m:
                thr = m.group(1); slow_n += int(m.group(2)); slow_ms += float(m.group(3))
                for k, v in zip(slow, m.groups()[3:9]):
                    slow[k] += float(v)
        elif l.startswith("[spirv-time]"):
            m = re.search(r"(\d+) translations: parse (\d+) ms, link\+io (\d+) ms, generate (\d+) ms", l)
            if m:
                n, a, b, c = map(int, m.groups()); spv = f"{n} translations {(a + b + c) / 1000:.1f} s ({(a + b + c) / n:.1f} ms each)"
        elif l.startswith("[vk-slow] submit: vkQueueSubmit"):
            m = re.search(r"vkQueueSubmit ([\d.]+) ms", l)
            if m:
                sub_n += 1; sub_ms += float(m.group(1))
    f = lambda d: " ".join(f"{k} {v / 1000:.1f}" for k, v in d.items() if k in ("pipe", "tex", "be"))
    print(f"{p}: {wall / 1000:.0f} s of stats | all ends, s: {f(tot)} | ends over {thr} ms: {slow_n}, {slow_ms / 1000:.1f} s: {f(slow)} "
          f"| slowest {mx:.0f} ms | {spv} | slow submits {sub_n}, {sub_ms / 1000:.1f} s")
