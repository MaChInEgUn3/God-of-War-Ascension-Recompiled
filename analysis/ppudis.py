import sys,struct
from capstone import Cs,CS_ARCH_PPC,CS_MODE_64,CS_MODE_BIG_ENDIAN
d=open("D:/GOW A/dump/EBOOT_enc.elf","rb").read()
a=int(sys.argv[1],16); b=int(sys.argv[2],16)
md=Cs(CS_ARCH_PPC,CS_MODE_64|CS_MODE_BIG_ENDIAN)
for i in md.disasm(d[a-0x10000:b-0x10000],a):
    print("%06x  %-8s %s"%(i.address,i.mnemonic,i.op_str))
