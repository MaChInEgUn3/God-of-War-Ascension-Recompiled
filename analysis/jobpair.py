"""jobpair.py <capture dir> [min gap] [max gap]: pair job captures (SPU_JOB_CAPTURE files) that are the same job one
frame apart and compare what they PUT.

A capture's signature is (entry pc, overlay, PUT sizes, GET sizes); its start local store from 0x4000 is hashed in
1 KB pages. Two captures of the same signature whose sequence numbers are min..max jobs apart (about one frame)
are paired when the later one shares the most pages with the earlier one. Per pair: are the PUT payloads equal;
if not, how many bytes differ and what the differing 4-byte words look like; how the start local store differs.
Private tool: the captures hold game data."""
import os, struct, sys
from collections import Counter, defaultdict

d = sys.argv[1]
GAP_LO = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
GAP_HI = int(sys.argv[3]) if len(sys.argv) > 3 else 2100
HDR = "version header_size ls_size gpr_count wid image_id resident_ovl resident_task spu_id policy_mode entry_pc link seq ms code_slots".split()


def load(path, want_ls=False):
    data = open(path, "rb").read()
    if data[:8] != b"SPUJCAP\0":
        return None
    h = dict(zip(HDR, struct.unpack_from("<5I3iIi5I", data, 8)))
    ls0 = h["header_size"]
    off = ls0 + h["ls_size"] + 16 * h["gpr_count"]
    puts, gets, ended = [], [], False
    while off + 32 <= len(data):
        typ, n, pc, a, b, c, dd, idx = struct.unpack_from("<8I", data, off)
        if typ == 2:
            puts.append((b, dd, data[off + 32:off + 32 + n]))
        elif typ == 1:
            gets.append((b, dd, data[off + 32:off + 32 + n]))
        elif typ == 15:
            ended = True
        off += 32 + n
    ls = data[ls0 + 0x4000:ls0 + h["ls_size"]]
    zero = bytes(256)
    pages = frozenset(hash(ls[i:i + 256]) for i in range(0, len(ls), 256) if ls[i:i + 256] != zero)
    j = dict(h=h, puts=puts, gets=gets, ended=ended, pages=pages, path=path,
             sig=(h["entry_pc"], h["resident_ovl"], tuple(p[1] for p in puts), tuple(g[1] for g in gets)))
    if want_ls:
        j["ls"] = ls
    return j


def fl(w):
    return struct.unpack(">f", struct.pack(">I", w))[0]


def word_class(x, y):
    fx, fy = fl(x), fl(y)
    okx = x == 0 or 1e-6 < abs(fx) < 1e7
    oky = y == 0 or 1e-6 < abs(fy) < 1e7
    if okx and oky:
        m = max(abs(fx), abs(fy))
        if m and abs(fx - fy) / m < 0.05:
            return "float, within 5%"
        return "float, further apart"
    if (x >> 28) in (2, 8, 0xC, 0xD) and (y >> 28) in (2, 8, 0xC, 0xD):
        return "address-like"
    return "other"


jobs = []
for name in sorted(os.listdir(d)):
    if name.endswith(".bin"):
        j = load(os.path.join(d, name))
        if j and j["ended"]:
            jobs.append(j)
jobs.sort(key=lambda j: j["h"]["seq"])
print(f"{len(jobs)} closed captures, seq {jobs[0]['h']['seq']}..{jobs[-1]['h']['seq']}, "
      f"t {jobs[0]['h']['ms'] / 1000:.2f}..{jobs[-1]['h']['ms'] / 1000:.2f} s")
by_sig = defaultdict(list)
for j in jobs:
    by_sig[j["sig"]].append(j)
print(f"{len(by_sig)} signatures; PUTs per job {sum(len(j['puts']) for j in jobs) / len(jobs):.1f}, "
      f"PUT bytes per job {sum(sum(p[1] for p in j['puts']) for j in jobs) / len(jobs):.0f}, "
      f"in-job GETs per job {sum(len(j['gets']) for j in jobs) / len(jobs):.2f}")
for sig, g in sorted(by_sig.items(), key=lambda kv: -len(kv[1]))[:6]:
    print(f"   {len(g):5d} jobs: entry 0x{sig[0]:05X} ovl {sig[1]} PUT sizes {list(sig[2])[:8]} GET sizes {list(sig[3])[:6]}")

# Blocks of 256 bytes of the start local store that few captures hold (a job's own streams, wherever the loader
# put them; leftovers of earlier jobs and the code are in many or in one): the same job a frame later shares them.
df = Counter()
for j in jobs:
    df.update(j["pages"])
for j in jobs:
    j["own"] = frozenset(h for h in j["pages"] if 2 <= df[h] <= 4)
MIN_SHARED = int(sys.argv[4]) if len(sys.argv) > 4 else 4
# "strict" as a fifth argument: keep a pair only when the partner is the single best one and shares at least 90%
# of the earlier job's blocks that come back a frame later at all (blocks also held by a capture within 200 jobs
# are leftovers of a neighbour, not the job's own).
STRICT = len(sys.argv) > 5 and sys.argv[5] == "strict"
if STRICT:
    holders = defaultdict(list)
    for j in jobs:
        for h in j["own"]:
            holders[h].append(j["h"]["seq"])
    for j in jobs:
        s = j["h"]["seq"]
        j["back"] = frozenset(h for h in j["own"]
                              if not any(0 < abs(o - s) <= 200 for o in holders[h])
                              and any(GAP_LO <= abs(o - s) <= GAP_HI for o in holders[h]))
pairs, used, weak = [], set(), 0
for sig, g in by_sig.items():
    for a in g:
        best, bestn, second = None, 0, 0
        key = "back" if STRICT else "own"
        for b in g:
            gap = b["h"]["seq"] - a["h"]["seq"]
            if gap < GAP_LO or gap > GAP_HI or id(b) in used:
                continue
            n = len(a[key] & b[key])
            if n > bestn:
                best, bestn, second = b, n, bestn
            elif n > second:
                second = n
        ok = best is not None and bestn >= MIN_SHARED
        if ok and STRICT:
            ok = second < bestn and bestn * 10 >= 9 * len(a["back"])
        if ok:
            used.add(id(best))
            pairs.append((a, best, bestn))
        elif best is not None:
            weak += 1
print(f"{len(pairs)} pairs {GAP_LO}..{GAP_HI} jobs apart sharing at least {MIN_SHARED} rare 256-byte blocks of the start LS "
      f"(mean {sum(p[2] for p in pairs) / max(1, len(pairs)):.1f}; rare blocks per job "
      f"{sum(len(j['own']) for j in jobs) / len(jobs):.1f}); {weak} more candidates shared fewer")

ident = 0
tot_bytes = diff_bytes = 0
wcls = Counter()
by_size = defaultdict(lambda: [0, 0, 0, 0])      # puts, identical puts, bytes, differing bytes
ea_same = ea_diff = 0
share_hist = Counter()
for a, b, n in pairs:
    all_same = True
    for (ea_a, sz, pa), (ea_b, _, pb) in zip(a["puts"], b["puts"]):
        if ea_a == ea_b:
            ea_same += 1
        else:
            ea_diff += 1
        s = by_size[sz]
        s[0] += 1
        s[2] += sz
        tot_bytes += sz
        if pa == pb:
            s[1] += 1
            continue
        all_same = False
        nd = sum(1 for x, y in zip(pa, pb) if x != y)
        s[3] += nd
        diff_bytes += nd
        for i in range(0, len(pa) - 3, 4):
            x, y = struct.unpack_from(">I", pa, i)[0], struct.unpack_from(">I", pb, i)[0]
            if x != y:
                wcls[word_class(x, y)] += 1
    if all_same:
        ident += 1
    share_hist[0 if all_same else 1] += 1
print(f"pairs whose PUT payloads are all byte-identical: {ident} of {len(pairs)}")
print(f"PUT bytes compared {tot_bytes}, differing {diff_bytes} ({100.0 * diff_bytes / max(1, tot_bytes):.1f}%); "
      f"PUTs to the same address {ea_same}, to another {ea_diff}")
print("differing 4-byte words: " + ", ".join(f"{k} {v}" for k, v in wcls.most_common()))
print("by PUT size (puts / identical / share of bytes differing in the rest):")
for sz, s in sorted(by_size.items(), key=lambda kv: -kv[1][2])[:14]:
    rest = (s[0] - s[1]) * sz
    print(f"   {sz:6d}: {s[0]:5d} / {s[1]:5d} / {100.0 * s[3] / rest if rest else 0.0:.1f}%")

# The in-job GETs of the pairs: equal payloads?
g_same = g_diff = 0
for a, b, n in pairs:
    for (ea_a, sz, pa), (ea_b, _, pb) in zip(a["gets"], b["gets"]):
        if pa == pb:
            g_same += 1
        else:
            g_diff += 1
print(f"in-job GET payloads of the pairs: equal {g_same}, different {g_diff}")
