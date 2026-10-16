import struct, sys, zlib, os, re

def open_psarc(fn):
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

    def read_entry(i):
        start, size, off = ents[i]
        f.seek(off)
        out = bytearray()
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
        return bytes(out[:size])
    names = [n for n in read_entry(0).decode("utf8", "replace").split("\n") if n]
    return names, ents, read_entry

def scan_spu(buf):
    res = []
    for m in re.finditer(b"\x7fELF\x01\x02\x01", buf):
        o = m.start()
        if buf[o + 18:o + 20] == b"\x00\x17":
            phoff = struct.unpack(">I", buf[o + 28:o + 32])[0]
            phn = struct.unpack(">H", buf[o + 44:o + 46])[0]
            end = 0
            for k in range(phn):
                q = o + phoff + k * 32
                t, po, va, pa, fsz, msz, fl, al = struct.unpack(">IIIIIIII", buf[q:q + 32])
                end = max(end, po + fsz)
            res.append((o, end))
    return res

mode = sys.argv[1]
if mode == "list":
    for fn in sys.argv[2:]:
        names, ents, _ = open_psarc(fn)
        for i, n in enumerate(names, 1):
            if re.search(r"\.(wad|self|elf|prx|sprx|spu)$", n, re.I) or re.search(r"spu|job|edge", n, re.I):
                print("%s\t%s\t%d" % (os.path.basename(fn), n, ents[i][1]))
else:
    fn, maxmb = sys.argv[2], int(sys.argv[3])
    names, ents, rd = open_psarc(fn)
    for i, n in enumerate(names, 1):
        if not re.search(r"\.(wad|ts|as|pak|bin)$", n, re.I):
            continue
        if ents[i][1] > maxmb * 1048576:
            print("SKIP big %s %.1fMB" % (n, ents[i][1] / 1e6))
            continue
        b = rd(i)
        r = scan_spu(b)
        if r:
            print("%s %.1fMB: %d SPU ELFs, bytes %d" % (n, len(b) / 1e6, len(r), sum(e for _, e in r)))
