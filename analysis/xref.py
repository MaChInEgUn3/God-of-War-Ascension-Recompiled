import struct,sys,re
d=open("D:/GOW A/dump/EBOOT_enc.elf","rb").read()
needle=sys.argv[1].encode()
vas=[m.start()+0x10000 for m in re.finditer(re.escape(needle),d[:0x7e33e8])]
print("string VAs:",[hex(v) for v in vas])
TOC=0x81ef60
def fo(va): return va-0x10000 if va<0x800000 else 0x7f0000+va-0x800000
# TOC entries equal to any string VA (4-byte)
hits=[]
for a in range(0x800000,0x83bf70-3,4):
    v=struct.unpack(">I",d[fo(a):fo(a)+4])[0]
    if v in vas: hits.append(a)
print("toc slots:",[hex(a) for a in hits])
n=0x617710//4; base=0x10230
w=struct.unpack(">%dI"%n,d[base-0x10000:base-0x10000+n*4])
for slot in hits:
    disp=slot-TOC
    for i,x in enumerate(w):
        if x>>26==32 and ((x>>16)&31)==2 and (x&0xFFFF)==(disp&0xFFFF):
            print("lwz r%d,%d(r2) at %x"%((x>>21)&31,disp,base+i*4))
# direct lis/addi
for v in vas:
    hi=(v+0x8000)>>16; lo=v&0xFFFF
    for i,x in enumerate(w):
        if x>>26==15 and (x&0xFFFF)==hi:
            for j in range(i+1,min(i+6,n)):
                y=w[j]
                if y>>26 in(14,24) and (y&0xFFFF)==lo: print("lis/addi at %x"%(base+i*4)); break
