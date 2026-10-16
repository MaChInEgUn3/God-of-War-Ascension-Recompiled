import struct,sys
d=open(sys.argv[1],"rb").read()
def va2off(va):
    if 0x10000<=va<0x10000+0x7e33e8: return va-0x10000
    if 0x800000<=va<0x800000+0x3bf70: return 0x7f0000+va-0x800000
    return None
a=0x62cbe0; end=0x62d02c

mods=[];tf=0
while a<end:
    o=va2off(a); e=d[o:o+0x2c]
    sz=e[0]; nf,nv,nt=struct.unpack(">HHH",e[6:12]); np_,nid,stub=struct.unpack(">III",e[0x10:0x1c])
    no=va2off(np_) if np_ else None
    name=d[no:d.index(b"\0",no)].decode("latin1") if no else "?"
    mods.append((name,nf,nv)); tf+=nf; a+=sz or 0x2c
for n,f,v in mods: print("  %-32s f=%4d v=%d"%(n,f,v))
print("modules",len(mods),"funcs",tf)
# OPD
text=(0x10000,0x10000+0x7e33e8)
o=0x808340-0x800000+0x7f0000; n=0xec20//8; ok=0; tocs=set()
for i in range(n):
    f,t=struct.unpack(">II",d[o+i*8:o+i*8+8])
    if text[0]<=f<text[1] and f%4==0: ok+=1; tocs.add(t)
print("opd entries",n,"valid-in-text",ok,"distinct toc",[hex(x) for x in list(tocs)[:4]])
