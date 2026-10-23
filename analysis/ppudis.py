"""ppudis.py <start hex> <end hex> [elf]: disassemble guest PPU code by virtual address.
Maps the address through the ELF64 program headers (default: the decrypted EBOOT in the VFS)."""
import sys, struct
from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN
path = sys.argv[3] if len(sys.argv) > 3 else "D:/GOW A/vfs/PS3_GAME/USRDIR/EBOOT.elf"
d = open(path, "rb").read()
a, b = int(sys.argv[1], 16), int(sys.argv[2], 16)
phoff, = struct.unpack_from(">Q", d, 0x20)
phentsize, phnum = struct.unpack_from(">HH", d, 0x36)
for i in range(phnum):
    p_type, _, p_off, p_vaddr, _, p_filesz = struct.unpack_from(">IIQQQQ", d, phoff + i * phentsize)
    if p_type == 1 and p_vaddr <= a < p_vaddr + p_filesz:
        md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
        md.skipdata = True          # an opcode capstone does not know must not end the listing
        o = p_off + (a - p_vaddr)
        for ins in md.disasm(d[o:o + (b - a)], a):
            print("%06x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
        break
else:
    print("address not in a loadable segment")
