import struct, sys, re, collections
p = sys.argv[1]
d = open(p, "rb").read()
print("size", len(d), "magic", d[:4])
ent = struct.unpack(">Q", d[24:32])[0]
phoff, shoff = struct.unpack(">QQ", d[32:48])
phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack(">HHHHH", d[54:64])
print("entry 0x%x phnum %d shnum %d" % (ent, phnum, shnum))
segs = []
for i in range(phnum):
    t, fl, off, va, pa, fsz, msz, al = struct.unpack(">IIQQQQQQ", d[phoff + i * phentsize:phoff + i * phentsize + 56])
    segs.append((t, fl, off, va, fsz, msz))
    print(" ph%d type 0x%x off 0x%x va 0x%x fsz 0x%x msz 0x%x" % (i, t, off, va, fsz, msz))

def va2off(va):
    for t, fl, off, v, fsz, msz in segs:
        if t == 1 and v <= va < v + fsz:
            return off + va - v
    return None

# sections
secs = {}
if shnum:
    raw = []
    for i in range(shnum):
        o = shoff + i * shentsize
        nm, ty, fl, addr, off, sz, lk, inf, al, es = struct.unpack(">IIQQQQIIQQ", d[o:o + 64])
        raw.append((nm, ty, addr, off, sz))
    stro = raw[shstrndx][3]
    for nm, ty, addr, off, sz in raw:
        name = d[stro + nm:d.index(b"\0", stro + nm)].decode()
        secs[name] = (addr, off, sz)
        print(" sec %-16s type %2d addr 0x%08x size 0x%x" % (name, ty, addr, sz))

# embedded ELFs
print("\n== embedded ELF magics")
cnt = collections.Counter()
spu = []
for m in re.finditer(b"\x7fELF", d):
    o = m.start()
    if o == 0:
        continue
    h = d[o:o + 0x34]
    if len(h) < 0x34:
        continue
    cls, endian = h[4], h[5]
    if endian == 2:
        et, em = struct.unpack(">HH", h[16:20])
    else:
        et, em = struct.unpack("<HH", h[16:20])
    cnt[(cls, endian, hex(em))] += 1
    if em == 0x17 and cls == 1 and endian == 2:
        spu.append(o)
print(dict(cnt))
print("SPU ELFs:", len(spu))
tot = 0
for o in spu[:200]:
    h = d[o:o + 0x34]
    entry = struct.unpack(">I", h[24:28])[0]
    phoff_s = struct.unpack(">I", h[28:32])[0]
    phn = struct.unpack(">H", h[44:46])[0]
    end = 0
    for k in range(phn):
        q = o + phoff_s + k * 32
        t, po, va, pa, fsz, msz, fl, al = struct.unpack(">IIIIIIII", d[q:q + 32])
        end = max(end, po + fsz)
    tot += end
    print("  spu@0x%x entry 0x%x phn %d size ~0x%x" % (o, entry, phn, end))
print("SPU ELF total bytes ~", tot)

# OPD
if ".opd" in secs:
    a, off, sz = secs[".opd"]
    n = sz // 24
    print("\n.opd entries:", n)
else:
    # heuristic: scan data seg for (func, toc) pairs where func is in text and 4-aligned
    n = 0
print()

# libstub / PRX param
for t, fl, off, va, fsz, msz in segs:
    if t == 0x60000002:
        sz, magic, ver, pad, libent_s, libent_e, libstub_s, libstub_e = struct.unpack(">IIIIIIII", d[off:off + 32])
        print("PRX_PARAM size", sz, "magic 0x%x" % magic, "libstub 0x%x-0x%x libent 0x%x-0x%x" % (libstub_s, libstub_e, libent_s, libent_e))
        a = libstub_s
        tf = 0
        mods = []
        while a < libstub_e:
            o = va2off(a)
            e = d[o:o + 0x2c]
            sz_, = struct.unpack(">B", e[:1])
            nfunc, nvar, ntls = struct.unpack(">HHH", e[6:12])
            nameptr, nidptr, stubptr = struct.unpack(">III", e[0x14:0x20])
            no = va2off(nameptr) if nameptr else None
            name = d[no:d.index(b"\0", no)].decode() if no else "?"
            mods.append((name, nfunc, nvar))
            tf += nfunc
            a += sz_ if sz_ else 0x2c
        for name, nf, nv in mods:
            print("  %-34s funcs %4d vars %d" % (name, nf, nv))
        print("TOTAL imported modules", len(mods), "functions", tf)
