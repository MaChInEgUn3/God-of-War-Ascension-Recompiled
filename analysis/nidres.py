import sys,struct
sys.path.insert(0,"D:/GOW A/tools/ps3recomp/tools")
import nid_database as nd
db=nd.get_default_db(); db.load_implemented("D:/GOW A/tools/ps3recomp")
print("db size",db.size, "check", hex(nd.compute_nid("cellSysmoduleLoadModule")))
d=open("D:/GOW A/dump/EBOOT_enc.elf","rb").read()
def v(va): return va-0x10000
a=0x62cbe0
import collections
tot=collections.Counter(); unk=collections.Counter()
while a<0x62d02c:
    e=d[v(a):v(a)+0x2c]; sz=e[0]; nf=struct.unpack(">H",e[6:8])[0]; np_,nidp,_=struct.unpack(">III",e[0x10:0x1c])
    name=d[v(np_):d.index(b"\0",v(np_))].decode()
    out=[]
    for i in range(nf):
        n=struct.unpack(">I",d[v(nidp)+i*4:v(nidp)+i*4+4])[0]
        r=db.lookup_nid(n); tot[name]+=1
        if not r: unk[name]+=1
        out.append(r[1] if r else "?%08x"%n)
    if name in ("cellSpurs","libvdec","cellGcmSys","cellAudio"): print("==",name,nf,"\n  "+"\n  ".join(out))
    a+=sz
print("unresolved per module:",dict(unk), "of", sum(tot.values()))
