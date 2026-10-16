"""List PSARC entry names matching a regex: psarc_find.py <regex> <psarc...>"""
import struct, sys, zlib, re
pat = re.compile(sys.argv[1], re.I)
for fn in sys.argv[2:]:
    f = open(fn, "rb")
    magic, ver, comp, toclen, esz, nent, bsz, flags = struct.unpack(">4sI4sIIIII", f.read(32))
    if magic != b"PSAR":
        print(fn, "not PSARC"); continue
    toc = f.read(toclen - 32)
    ents = []
    for i in range(nent):
        e = toc[i * esz:(i + 1) * esz]
        ents.append((struct.unpack(">I", e[16:20])[0], int.from_bytes(e[20:25], "big"), int.from_bytes(e[25:30], "big")))
    bw = 2 if bsz <= 0x10000 else (3 if bsz <= 0x1000000 else 4)
    bt = toc[nent * esz:]
    sizes = [int.from_bytes(bt[i:i + bw], "big") for i in range(0, len(bt) - bw + 1, bw)]
    start, size, off = ents[0]
    f.seek(off); out = b""; k = start
    while len(out) < size:
        cs = sizes[k]; k += 1
        want = min(bsz, size - len(out))
        raw = f.read(bsz if cs == 0 else cs)
        out += raw[:want] if cs == 0 else (zlib.decompress(raw) if cs != want else raw)
    names = [n for n in out.decode("utf8", "replace").split("\n") if n]
    hits = [(n, ents[i][1]) for i, n in enumerate(names, 1) if i < len(ents) and pat.search(n)]
    print("== %s: %d names, %d hits" % (fn.split("/")[-1], len(names), len(hits)))
    for n, s in hits[:15]:
        print("   %s %d" % (n, s))
