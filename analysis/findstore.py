import sys,struct,re
from capstone import Cs,CS_ARCH_PPC,CS_MODE_64,CS_MODE_BIG_ENDIAN
d=open("D:/GOW A/dump/EBOOT_enc.elf","rb").read()
target=int(sys.argv[1],16)
hi=(target+0x8000)>>16; lo=target-(hi<<16)
base=0x10230; size=0x617710
# fast pre-scan: find lis rX,hi words, then check next 64 instrs for a mem op with disp lo on that reg
W=struct.unpack('>%dI'%(size//4), d[base-0x10000:base-0x10000+size])
res=[]
want_lo=lo & 0xFFFF
LIS_OPC=15  # addis
for idx,w in enumerate(W):
    if (w>>26)==15 and ((w>>16)&0x1F)==0 and (w&0xFFFF)==hi:
        rt=(w>>21)&0x1F
        for j in range(idx+1,min(idx+120,len(W))):
            x=W[j]; op=x>>26
            if op in (32,36,34,38,40,44,58,62,33,37,35,39,41,45,14,12,24):  # lwz stw lbz stb lhz sth ld std..., addi addic ori
                ra=(x>>16)&0x1F
                if ra==rt and (x&0xFFFF)==want_lo:
                    names={32:'lwz',36:'stw',34:'lbz',38:'stb',40:'lhz',44:'sth',58:'ld',62:'std',33:'lwzu',37:'stwu',35:'lbzu',39:'stbu',41:'lhzu',45:'sthu',14:'addi',12:'addic',24:'ori'}
                    res.append((base+4*j,names[op],'r%d'%((x>>21)&0x1F),'lis@%x'%(base+4*idx)))
            # stop if rt is overwritten
            if op in (15,14,32,24) and ((x>>21)&0x1F)==rt and not ( (x>>16)&0x1F==rt and op!=15) and op==15:
                break
for a,n,r,l in res: print("%06x %-5s %s %s %s"%(a,n,r,l,'WRITE' if n.startswith('st') else ''))
