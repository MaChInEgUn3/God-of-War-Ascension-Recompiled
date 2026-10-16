# Status (updated 2026-10-16)

## Works
* Loader, lifter, compile and link of the whole PPU; boot through CRT, FIOS, PSARC mounting, DCDS, savedata.
* 12+ WWS SPURS workloads registered; Edge Zlib served by HLE (PSARC blocks inflate correctly).
* Optional **firmware SPURS (LLE)**: the real `libsre` runs; the real SPURS kernel and system-service policy module
  execute as lifted SPU code.
* Vulkan backend on Windows (ported), GCM init, display buffers, about 890 flips; frames are black (clear only).

## Current blocker (root-cause chain, measured)
1. Boot function `0x47924` loads resource `R_Perm`; its heap `WAD_R_Perm` is created with **size 0**.
2. The size comes from `0x3AD48`: a hash lookup of `WAD_<name>` in a table whose pointer is the result of
   `0x339054("WST_Global")`, a lookup in an asset-set registry. `WST_Global` is **not registered**.
3. A size-0 heap corrupts its free list, the allocator returns pointers `+0x10000000`, `Free` is routed to the
   mapped-page allocator, and `UnmapMainMemory(ptr, 0)` fails: `MemTable entry not found` (game asserts are ignored traps).

**Next step:** trace `0x338F28` / `0x338E70` and find who loads and registers the global WST asset set before `0x47924`
(a silent PSARC/WAD read failure? the language archive `ENGLISH.PSARC` not mounted?).

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.9): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
