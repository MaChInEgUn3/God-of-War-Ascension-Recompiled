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
All 19 SPURS workloads run (CellSpurs2 layout fixed: `flags1` at 0x74 with `SF1_32_WORKLOADS`, flag receiver at 0x77,
signal2 at 0x78; `cellSpursGetWorkloadFlag`/`_cellSpursWorkloadFlagReceiver` implemented; kernel `tempArea` filled).
EDGE2's job batch completes, but the next EDGE0 (wid 15) / EDGE1 (wid 16) batches never zero their completion counters
(`0x99AAC8`/`0x99AAC0`); the main thread waits at `0x3E7A14`. Only 2 flips so far (RSX gets almost no commands).
Measured: the EDGE policy-module runs (wids 15-17) never show WWS pipeline activity (`SPURS_PM_PIPE`), while wid 8
(BrBDblBufWrkld) does; keeping `readyCount` (`SPURS_KEEP_READY=1`) does not change it; no SPU spins. The game pokes
readyCount for wid 16 (=4). Next: find what the WWS PM reads to pick up a job list (header at data-0x80, word +0x2C
goes 0x1 -> 0x10 at the kick) and why the EDGE PMs see none; check the RSX side (FIFO get/put, `wklFlag` +0x6C never
written by the HLE RSX).
Update (M0.14): WWS job-list format decoded (cmd 1 job, 2 decrement counter, 5 wait counter, 3 nop). RsxSync is stuck
waiting on dependency counter `0x2001ED1C` (set to 1 by `func_003B3028`); nothing in memory references it for release,
so the releaser (likely an RSX semaphore to main memory, not yet emulated) has not run. See FEASIBILITY M0.14.
Update (M0.15): the releaser is `NV4097_GET_REPORT` with the report DMA context = host buffer (0xFEED0001); now emulated,
RsxSync advances (1 -> 4 entries). Next stop: SPU BRANCH-TO-0 in an RsxSync job at LS 0x4264; EDGE0/1 counters still 1.
Update (M0.16): that BRANCH-TO-0 was the job's `stopd` assert after a failed SPU-side `cellSpursSendSignal`;
`cellSpursCreateTask2` had the wrong ABI and never registered the 5 tasks of ELF `0x68F580`. Fixed (forwarded to
`cellSpursCreateTask`), the task ELF is lifted (image 14) and the tasks run. The GCM FIFO walker no longer abandons
GoW:A's self-JUMP park (`GCM_PARK_WAIT=1`, default in the port), so later GET_REPORTs execute and RsxSync consumes
its whole list. Current stop: EDGE0/1/2 jobs enter the WWS pipeline but never complete; per-frame batch counters
`0x99AA88/8C/90` stay at 5/5/6 and the main thread waits in `func_003E799C`.

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.13): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
