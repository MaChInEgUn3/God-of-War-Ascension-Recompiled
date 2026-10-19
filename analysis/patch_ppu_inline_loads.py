#!/usr/bin/env python3
"""Give an older PPU lift the inline guest loads of the current ppu_lifter.py.

The lifter's header now defines vm_read{8,16,32,64}_inl and maps vm_read*() to them,
so a lifted load is a bounds check plus a load instead of a call into the runtime's
out-of-line accessor (and its diagnostic probes). A lift made before that only
declares the out-of-line functions. This copies the block from the lifter's
HEADER_PREAMBLE into the existing ppu_recomp.h, right after the memory helper
declarations, so the PPU sources do not have to be lifted again.

The runtime turns the inline path off (vm_inline_ok = 0) whenever a read diagnostic
such as PPU_RWATCH or PPU_HOTMAP is set, and the out-of-line accessor still serves
the null page, the raw SPU windows, the GCM ref-poll word and the end of the map.
Build with -DPPU_NO_INLINE_LOADS to get the old behaviour back.

Usage: python analysis/patch_ppu_inline_loads.py <ps3recomp> <recompiled>/ppu_recomp.h
"""
import importlib.util, os, sys

START = "/* Inline guest-load fast paths."
END = "#define vm_read64(a) vm_read64_inl(a)\n#endif\n"
ANCHOR = "int      ppu_stdcx64(uint64_t addr, uint64_t expected, uint64_t val);\n#ifdef __cplusplus\n}\n#endif\n"

def main():
    sdk, header = sys.argv[1], sys.argv[2]
    spec = importlib.util.spec_from_file_location("ppu_lifter", os.path.join(sdk, "tools", "ppu_lifter.py"))
    lifter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lifter)
    pre = lifter.HEADER_PREAMBLE
    i, j = pre.find(START), pre.find(END)
    if i < 0 or j < 0:
        sys.exit("inline-load block not found in ppu_lifter.HEADER_PREAMBLE")
    block = pre[i:j + len(END)]
    text = open(header, encoding="utf8").read()
    if "vm_read32_inl" in text:
        print("already patched")
        return
    k = text.find(ANCHOR)
    if k < 0:
        sys.exit("memory helper declarations not found in " + header)
    k += len(ANCHOR)
    text = text[:k] + block + text[k:]
    open(header, "w", encoding="utf8", newline="\n").write(text)
    print(f"inserted {block.count(chr(10))} lines into {header}")

if __name__ == "__main__":
    main()
