import re,struct,sys,hashlib,os
d=open(sys.argv[1],"rb").read(); out=sys.argv[2]; os.makedirs(out,exist_ok=True)
mods=[]
for m in re.finditer(b"\xc0\xde\xc0\xde",d):
    s=m.start()-0x14
    w=struct.unpack(">5I",d[s:s+20])   # ila | entry | size | 0 | 0
    mods.append((s,w))
tot=0;ok=0
print("module: file_off  va  ila  entry  size")
for s,w in mods:
    ila,entry,size,z1,z2=w
    va=s+0x10000
    good=(ila>>24 in(0x42,0x41,0x40)) and 0<size<0x40000 and z1==0 and z2==0
    ok+=good; tot+=size if good else 0
    open(f"{out}/wws_{va:06x}_e{entry:x}.bin","wb").write(d[s:s+size+0x20] if good else b"")
    print("  va=0x%06x ila=%08x entry=0x%x size=0x%x %s"%(va,ila,entry,size,"" if good else "BAD"))
print("modules",len(mods),"well-formed",ok,"total image bytes",tot)
