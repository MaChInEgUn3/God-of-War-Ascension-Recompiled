import struct, sys
d = open(sys.argv[1] + "/EBOOT.BIN", "rb").read()
magic, ver, ktype, flags, meta_off, hdr_len, enc_len = struct.unpack(">4sIHHIQQ", d[:0x20])
print("SELF", magic, "ver", ver, "key_type 0x%x" % ktype, "flags 0x%x" % flags, "hdr_len 0x%x" % hdr_len, "enc_len 0x%x" % enc_len)
sh_off, ph_off, sec_off, ver_off, sup_off, ctl_off, ctl_sz = struct.unpack(">QQQQQQQ", d[0x20:0x20 + 0x38]) if False else (0,)*7
# extended header at 0x20
appinfo_off, elf_off, phdr_off, shdr_off, sinfo_off, ver_off, sup_off, ctl_off, ctl_sz, pad = struct.unpack(">QQQQQQQQQQ", d[0x28:0x28 + 0x50])
print("elf_off 0x%x phdr_off 0x%x shdr_off 0x%x" % (elf_off, phdr_off, shdr_off))
aid, vsid, atype, av = struct.unpack(">QIIQ", d[appinfo_off:appinfo_off + 0x18])
print("auth_id 0x%x vendor 0x%x app_type %d (1=lv2,2=lv1? 4=app,5=isolated) sdk_ver/appver 0x%x" % (aid, vsid, atype, av))
e = d[elf_off:elf_off + 0x40]
print("ELF ident", e[:4], "class", e[4], "endian", e[5], "type 0x%x" % struct.unpack(">H", e[16:18])[0], "machine 0x%x" % struct.unpack(">H", e[18:20])[0])
entry = struct.unpack(">Q", e[24:32])[0]
phoff, shoff = struct.unpack(">QQ", e[32:48])
phentsize, phnum = struct.unpack(">HH", e[54:58])
print("entry 0x%x phnum %d" % (entry, phnum))
tot = 0
for i in range(phnum):
    o = phdr_off + i * phentsize
    p_type, p_flags, p_off, p_va, p_pa, p_fsz, p_msz, p_al = struct.unpack(">IIQQQQQQ", d[o:o + 56])
    print(" ph%d type 0x%08x flags %d off 0x%x va 0x%x filesz 0x%x memsz 0x%x" % (i, p_type, p_flags, p_off, p_va, p_fsz, p_msz))
# SFO
s = open(sys.argv[1] + "/PARAM.SFO", "rb").read()
kt, dt, n = struct.unpack("<III", s[8:20])
for i in range(n):
    ko, fmt, ln, mx, do = struct.unpack("<HHIII", s[20 + 16 * i:36 + 16 * i])
    k = s[kt + ko:s.index(b"\0", kt + ko)].decode()
    v = s[dt + do:dt + do + ln]
    print("SFO", k, v.rstrip(b"\0").decode("utf8", "replace") if fmt == 0x204 else struct.unpack("<I", v[:4])[0])
