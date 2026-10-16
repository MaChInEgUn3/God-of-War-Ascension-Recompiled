import struct, sys, zlib, collections, re, os
pat = re.compile(r"spu|\.self|\.elf|\.prx|\.sprx|job|spurs|edge|\.vp|\.fp|\.cgb|\.vpo|\.fpo|shader|\.lua|\.sc$", re.I)
for fn in sys.argv[1:]:
    f = open(fn, "rb")
    h = f.read(32)
    magic, ver, comp, toclen, esz, nent, bsz, flags = struct.unpack(">4sI4sIIIII", h)
    assert magic == b"PSAR", fn
    f.seek(32)
    toc = f.read(toclen - 32)
    ents = []
    for i in range(nent):
        e = toc[i * esz:(i + 1) * esz]
        start = struct.unpack(">I", e[16:20])[0]
        size = int.from_bytes(e[20:25], "big")
        off = int.from_bytes(e[25:30], "big")
        ents.append((start, size, off))
    bw = 2 if bsz <= 0x10000 else (3 if bsz <= 0x1000000 else 4)
    bt = toc[nent * esz:]
    sizes = [int.from_bytes(bt[i:i + bw], "big") for i in range(0, len(bt) - bw + 1, bw)]

    def read_entry(i):
        start, size, off = ents[i]
        f.seek(off)
        out = b""
        k = start
        while len(out) < size:
            cs = sizes[k]
            k += 1
            want = min(bsz, size - len(out))
            if cs == 0:
                out += f.read(bsz)[:want]
            else:
                raw = f.read(cs)
                out += zlib.decompress(raw) if cs != want else raw
        return out[:size]
    names = read_entry(0).decode("utf8", "replace").split("\n")
    names = [n for n in names if n]
    ext = collections.Counter()
    extsz = collections.Counter()
    hits = []
    for i, n in enumerate(names, 1):
        if i >= len(ents):
            break
        e = os.path.splitext(n)[1].lower() or "(none)"
        ext[e] += 1
        extsz[e] += ents[i][1]
        if pat.search(n):
            hits.append((n, ents[i][1]))
    print("== %s files=%d total=%.1fMB" % (os.path.basename(fn), len(names), sum(e[1] for e in ents[1:]) / 1e6))
    print("   ext:", ", ".join("%s:%d(%.1fMB)" % (k, v, extsz[k] / 1e6) for k, v in ext.most_common(14)))
    for n, s in hits[:40]:
        print("   HIT %s %d" % (n, s))
    if len(hits) > 40:
        print("   ... %d hits total" % len(hits))
