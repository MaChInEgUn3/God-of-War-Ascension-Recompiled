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
   `0x339054("WST_Global")`, a lookup in the DC (data-config) symbol registry. `WST_Global` is a symbol of
   `/DC/PS3_BE/WAD_R_FX.DCB` (in `GAME.PSARC`) and **is not registered**: the registry object `DC_WAD_R_FX` is created
   but never bound to its data (`+0x10`/`+0x14` stay 0), so every lookup (also `GBL_ProjectSettings`) returns NULL.
3. Why it is not bound: the DCB parser (`0x3C93E8`, 509 iterations) keeps reading the **same first record**; the
   stream read position (`0x2A8A94` -> `0x2A7EF8`, file object at `0x9998AC`) does not advance, so the type-12/13
   handlers (`0x338C28`, `0x338D2C`) are never dispatched.
4. A size-0 heap then corrupts its free list, `Free` is routed to the mapped-page allocator and
   `UnmapMainMemory(ptr, 0)` fails: `MemTable entry not found` (game asserts are ignored traps).

**Next step:** find why the stream read primitive `0x2A7EF8` returns the start of the DCB each time (compare with the
`DEPENDENCYGRAPH.IFF` read, which works). Tools: `PPU_TRACE_FUNCS=all` + `PPU_TRACE_ADDRS`, `GOW_PEEK`
(see [DEBUGGING.md](DEBUGGING.md)).

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.10): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
