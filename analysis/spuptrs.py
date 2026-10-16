import struct,collections,sys
d=open(sys.argv[1],"rb").read()
LO,HI=0x681880,0x7f3380
def fo(va): return va-0x10000 if va<0x800000 else 0x7f0000+va-0x800000
# scan 4-aligned words in whole file image regions: rodata+data outside SPU region itself, and inside
tg=collections.Counter(); src=collections.defaultdict(list)
regions=[("rodata_pre",0x627940,0x681880),("data",0x800000,0x83bf70),("spu_region",0x681880,0x7f3380)]
for nm,a,b in regions:
    o=fo(a); n=(b-a)//4
    w=struct.unpack(">%dI"%n,d[o:o+n*4])
    for i,x in enumerate(w):
        if LO<=x<HI and x%16==0:
            tg[x]+=1; src[x].append((nm,a+i*4))
print("distinct 16B-aligned targets into SPU region:",len(tg))
by=collections.Counter()
for x,s in src.items():
    for nm,_ in s: by[nm]+=1
print("pointer sources by region:",dict(by))
ts=sorted(tg)
print("first 40 targets:",[hex(x) for x in ts[:40]])
# gaps -> candidate blob sizes
gaps=[ts[i+1]-ts[i] for i in range(len(ts)-1)]
print("n gaps",len(gaps),"median",sorted(gaps)[len(gaps)//2] if gaps else 0,"max",max(gaps) if gaps else 0)
