import re,sys,struct,collections
d=open(sys.argv[1],"rb").read()
offs=[0x671880,0x67f580,0x689000,0x693380]
for o in offs:
    ph=struct.unpack(">I",d[o+28:o+32])[0]; n=struct.unpack(">H",d[o+44:o+46])[0]
    segs=[]
    for k in range(n):
        t,po,va,pa,fs,ms,fl,al=struct.unpack(">IIIIIIII",d[o+ph+k*32:o+ph+k*32+32]); segs.append((t,hex(va),hex(fs),hex(ms)))
    blob=d[o:o+0xe000]
    ss=[m.group().decode() for m in re.finditer(rb"[ -~]{6,}",blob)]
    print(hex(o),segs,ss[:8])
# chunk stats of rodata region
base=0x69c000
print("-- region 0x69c000..0x7e33e8 (file offs) per 128KB: printable%% | zero%% | lq/stq-like words%%")
step=0x20000
for a in range(base,0x7e3380,step):
    c=d[a:a+step]; pr=sum(1 for b in c if 32<=b<127 or b in(9,10,13))/len(c); z=c.count(0)/len(c)
    w=struct.unpack(">%dI"%(len(c)//4),c[:len(c)//4*4])
    # SPU-ish: lqd(0x34),stqd(0x24),shufb(0x8x top nibble 1000 +?),ila(0x42..0x43)
    spu=sum(1 for x in w if (x>>24) in (0x34,0x24,0x42,0x43,0x40,0x41)) /len(w)
    print(hex(a),"print %.2f zero %.2f spuish %.2f"%(pr,z,spu))
