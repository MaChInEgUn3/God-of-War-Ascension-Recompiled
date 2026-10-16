import struct,sys
d=open("D:/GOW A/dump/EBOOT_enc.elf","rb").read()
target=int(sys.argv[1],16)
n=0x617710//4; base=0x10230
w=struct.unpack(">%dI"%n,d[base-0x10000:base-0x10000+n*4])
out=[]
for i,x in enumerate(w):
    if x>>26==18 and (x&1) and not (x&2):
        off=x&0x03FFFFFC
        if off&0x02000000: off-=0x04000000
        pc=base+i*4
        if (pc+off)&0xFFFFFFFF==target: out.append(pc)
print(len(out),[hex(a) for a in out[:40]])
