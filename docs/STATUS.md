# Status (updated 2026-10-16)

## Works
* Loader, lifter, compile and link of the whole PPU; boot through CRT, FIOS, PSARC mounting, DCDS, savedata.
* 12+ WWS SPURS workloads registered; Edge Zlib served by HLE (PSARC blocks inflate correctly).
* Optional **firmware SPURS (LLE)**: the real `libsre` runs; the real SPURS kernel and system-service policy module
  execute as lifted SPU code.
* Vulkan backend on Windows (ported), GCM init, display buffers, about 890 flips; frames are black (clear only).

## Resolved (M0.11): the GCM `MemTable entry not found` blocker
Root cause: the Edge Zlib HLE (`cellSpursLFQueuePushBody`) ignored the slice offset (`w6 & 0xFFFF`) of a partial
inflate request, so FIOS slice reads of compressed PSARC entries always got the block prefix. The `.dcb` parser re-read
its first record, the DC symbol registry was never bound (`WST_Global` unresolved), heap `WAD_R_Perm` got size 0 and the
heap corruption ended in `UnmapMainMemory` failures. Fixed in the patch; see [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md) M0.10/M0.11.

## Resolved (M0.12): WWS SPU jobs run; SPURS has 32 workload slots
The WWS job-manager interrupt handler (which builds a register save/restore shim at runtime) now runs: fixed the
microstep `stqa`/`lqa` address scale, the trampoline re-arm after interpreter hand-back, and the interpreter's missing
ops (`bisled`, E/D bits, double precision, ...). The job pipeline advances and the PPU job waits release. SPURS HLE
now models `CellSpurs2` (32 workloads); GoW:A registers 19 (EDGE2 and RsxSync were never scheduled before).

## Current blocker
An SPU (WWS PM, image 10) spins on `GETLLAR ea=0` at LS `0x4194`, the RSX sees `GET_REPORT idx=1048575`, and the main
thread waits on a render job set (`0x21600530`, via `0x466E38`). Likely a job with a null EA (RSX report/label sync).
Also open: NULL+0x40 reads in `0x37DE80`, unresolved NIDs `0xA73BF47E`, `0xC765B995`.

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.12): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
