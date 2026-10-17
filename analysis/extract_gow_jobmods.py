#!/usr/bin/env python3
"""Carve GoW:A's WWS SPU job-code modules out of the decrypted EBOOT.

GoW:A embeds its 33 job modules raw (not zlib) in the PPU image. A module is
`4 x ila | entryOffset (+0x10) | imageSize (+0x14) | ... | 0xC0DEC0DE (+0x20)`;
the initialised (code + rodata) image is imageSize - 32 (bss follows in LS). The
job manager stages a module at LS 0x4000 and enters it at 0x4000 + entryOffset.

Output: <out>/jobmod_<sha1[:12]>_e<entryOffset>.bin, the layout
tools/ps3recomp/tools/lift_jobmods.py consumes. The output is derived from the
game: keep it out of the repository.

Usage: python analysis/extract_gow_jobmods.py <EBOOT_enc.elf> <outdir>
"""
import hashlib, os, struct, sys

def main():
    elf, out = sys.argv[1], sys.argv[2]
    d = open(elf, "rb").read()
    os.makedirs(out, exist_ok=True)
    starts = []
    i = d.find(b"\xc0\xde\xc0\xde")
    while i >= 0:
        s = i - 0x20
        if s >= 0 and d[s] in (0x42, 0x43):
            starts.append(s)
        i = d.find(b"\xc0\xde\xc0\xde", i + 1)
    total = 0
    for k, s in enumerate(starts):
        entry, size = struct.unpack_from(">II", d, s + 0x10)
        n = size - 32
        if k + 1 < len(starts):
            n = min(n, starts[k + 1] - s)       # never run into the next module
        raw = d[s:s + n]
        sha = hashlib.sha1(raw).hexdigest()[:12]
        open(os.path.join(out, f"jobmod_{sha}_e{entry:x}.bin"), "wb").write(raw)
        total += n
        print(f"  file 0x{s:06X} entry 0x{entry:05X} imageSize 0x{size:05X} carved 0x{n:05X} sig {raw[:16].hex()}")
    print(f"{len(starts)} modules, {total} bytes -> {out}")

if __name__ == "__main__":
    main()
