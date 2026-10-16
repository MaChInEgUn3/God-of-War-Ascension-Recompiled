"""Extract one entry: psarc_extract.py <psarc> <name> <out>"""
import struct, sys, zlib
fn, want_name, outp = sys.argv[1:4]
f = open(fn, "rb")
magic, ver, comp, toclen, esz, nent, bsz, flags = struct.unpack(">4sI4sIIIII", f.read(32))
toc = f.read(toclen - 32)
ents = []
for i in range(nent):
    e = toc[i * esz:(i + 1) * esz]
    ents.append((struct.unpack(">I", e[16:20])[0], int.from_bytes(e[20:25], "big"), int.from_bytes(e[25:30], "big")))
bw = 2 if bsz <= 0x10000 else (3 if bsz <= 0x1000000 else 4)
bt = toc[nent * esz:]
sizes = [int.from_bytes(bt[i:i + bw], "big") for i in range(0, len(bt) - bw + 1, bw)]
def rd(i):
    start, size, off = ents[i]; f.seek(off); out = b""; k = start
    while len(out) < size:
        cs = sizes[k]; k += 1; want = min(bsz, size - len(out))
        raw = f.read(bsz if cs == 0 else cs)
        out += raw[:want] if cs == 0 else (zlib.decompress(raw) if cs != want else raw)
    return out
names = [n for n in rd(0).decode().split("\n") if n]
i = [n.upper() for n in names].index(want_name.upper()) + 1
open(outp, "wb").write(rd(i)); print("wrote", outp)
