import struct,re,hashlib,sys,collections
d=open(sys.argv[1],"rb").read()
shoff=struct.unpack(">Q",d[40:48])[0]; n=31
S=[]
for i in range(n):
    nm,ty,fl,addr,off,sz,lk,inf,al,es=struct.unpack(">IIQQQQIIQQ",d[shoff+i*64:shoff+i*64+64]); S.append((nm,ty,addr,off,sz))
st=S[-1]; strt=d[st[3]:st[3]+st[4]]
print("-- sections")
for i,(nm,ty,addr,off,sz) in enumerate(S):
    name=strt[nm:strt.index(b"\0",nm)].decode()
    if sz>0x10000 or i in(14,15,16): print(i,name,hex(addr),hex(sz))
# exact SPU word counts in section 15 vs PPU .text
def cnt(a,b):
    w=struct.unpack(">%dI"%((b-a)//4),d[a:a+(b-a)//4*4]); c=collections.Counter(w)
    return {k:c[v] for k,v in dict(bi_lr=0x35000000,stqd_lr=0x24004080,lnop=0x00200000,nop=0x40200000).items()},len(w)
ppu=(0x10230-0x10000,0x10230-0x10000+0x617710)
print("PPU .text",cnt(*ppu))
for nm,a,b in (("sec15 0x681880",0x671880,0x671880+0x171b00),("sec15 after 4 ELFs",0x69c000,0x671880+0x171b00)):
    print(nm,cnt(a,b))
# cellSpurs NIDs
tbl=0x62c670 # not necessarily; read from stub
def va2off(va):
    if 0x10000<=va<0x10000+0x7e33e8: return va-0x10000
    return None
a=0x62cbe0;end=0x62d02c
suffix=bytes.fromhex("6759659904250490564427499489741A")
def nid(name): return hashlib.sha1(name.encode()+suffix).digest()[:4]
names=set()
import glob,os
for f in glob.glob("D:/GOW A/tools/ps3recomp/**/*.[ch]",recursive=True)+glob.glob("D:/GOW A/tools/ps3recomp/tools/*.py")+glob.glob("D:/GOW A/tools/ps3recomp/**/*.json",recursive=True):
    try: t=open(f,errors="ignore").read()
    except: continue
    names.update(re.findall(r"\b(?:cellSpurs\w+|cellSpursJq\w+|_cellSpurs\w+|cellSpursJobChain\w+)\b",t))
db={nid(x):x for x in names}
print("candidate names",len(names))
while a<end:
    o=va2off(a); e=d[o:o+0x2c]; sz=e[0]
    nf=struct.unpack(">H",e[6:8])[0]; np_,nidp,stub=struct.unpack(">III",e[0x10:0x1c])
    no=va2off(np_); name=d[no:d.index(b"\0",no)].decode()
    if name=="cellSpurs":
        res=[]
        for i in range(nf):
            nb=d[va2off(nidp)+i*4:va2off(nidp)+i*4+4]
            res.append(db.get(nb[::-1]) or db.get(nb) or "?"+nb.hex())
        print(len(res)); print("\n".join(res))
    a+=sz or 0x2c
