# Architecture

## Pipeline

```
 disc dump --decrypt--> EBOOT.elf -+- ppu_loader/ppu_lifter (ps3recomp) --> lifted C++ (local/recompiled)
                                   +- SPU blobs carved by analysis/make_spu_port.py --> spu_lifter --> lifted C (port/spu, local)
 firmware (own dump, optional) --decrypt--> libsre.prx --> lift_prx.py + ppu_lifter --> local/lle/recomp
 port/ (this repo) + ps3recomp runtime (patched) + lifted code --> MyGameRecomp.exe (clang-cl, Vulkan backend)
```

## Runtime model (from ps3recomp)

* **PPU**: every guest function is a host function `func_XXXXXXXX(ppu_context*)`; direct calls are host calls,
  indirect calls go through an address -> function hash table. Unconditional PowerPC traps (`twu`) are *ignored*
  by the lifter, so game asserts do not stop the process (see docs/DEBUGGING.md).
* **System libraries**: HLE (`libs/*`), selected by NID (`ps3_hle_call`). A loaded PRX can take precedence per NID
  (`prx_resolve_export`); this is used for the optional firmware SPURS (LLE).
* **SPU**: each SPU image is lifted to C and registered by FNV-1a-64 fingerprint of its raw bytes. SPURS policy
  modules and kernels are overlays selected at DMA time (`spu_overlay_register_source`).
* **RSX**: the command FIFO is drained by the Vulkan (or D3D12) backend. GCM offset tables are mirrored into guest memory.
* **Memory**: flat 4 GB host reservation, lazily committed.

## This port

* `port/main.cpp`: runner (PARAM.SFO -> title id, picks Vulkan, starts the sampler).
* `port/stubs.cpp`: `ps3_load_prx_modules()` moves the GCM inject window to `0x70000000` (the game reserves a
  512 MB pool at `0x20000000`, like real lv2) and optionally loads the firmware SPURS.
* `port/lle_libsre.cpp`: loads the user's lifted `libsre` at `0x30000000`, registers PM overlays, keeps the
  task/taskset/eventflag/LFQueue family on HLE (`lle_keep_hle.inc`, generated from the exports) and exposes
  `GOW_LLE=off|all-hle|real`.
* `port/gow_mlaa_hle.c`: HLE of the SPU MLAA taskset (ELF `0x68F580`, image 14). Each batch is served at the
  moment the RsxSync job signals the tasks (title line-commit hook): RSX label = batch number, then the per-frame
  counter decrement (and the workload kick at zero). No anti-aliasing; `GOW_MLAA=lifted` runs the real tasks.
* `port/main.cpp` also sets title defaults for runtime knobs: `GCM_PARK_WAIT=1`, `GCM_SEMA_ACQUIRE=1`.
* `patches/`: fixes against upstream (envp/envc, SPURS v2 wrappers, `sys_lwcond_t` layout, 64-bit file sizes,
  Edge Zlib HLE in `_cellSpursLFQueuePushBody`, GCM IO allocator, syscall 14 range, `sys_mmapper_allocate_address`
  first-fit at `0x20000000`) plus diagnostics (function-entry tracer in the lifter, MemTable dump, stack scan).

## Game facts (measured)

* ELF64 BE PPC64, 16,336 recovered functions, 25 imported modules / 345 imports.
* SPU: 4 ELF images + 33 `0xC0DEC0DE` WWS job modules; Sony WWS Job Manager, ICE BatchJob, Edge (Zlib, Geometry), MLAA.
* Data: about 36 PSARC archives (zlib blocks), mounted through FIOS.
* Engine memory: `Ice::Render` MemTable (key = EA page, size pages, IO page), heap objects tagged `0xC0DE1111`,
  mapped-page allocator tagged `0x2D1E4`.
