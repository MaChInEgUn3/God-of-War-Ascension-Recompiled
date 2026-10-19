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
Update (M0.17): EDGE was waiting (list cmd 2) for counters that the SPU MLAA tasks release. The RSX now blocks on
NV406E SEMAPHORE_ACQUIRE (`GCM_SEMA_ACQUIRE=1`, port default) and the MLAA taskset is served by HLE at signal time
(`port/gow_mlaa_hle.c`; **frames are not anti-aliased**; `GOW_MLAA=lifted` restores the real, ~100x too slow, tasks).
Batches flow and EDGE0/1/2 run jobs. Current stop: `func_00397328` loops on NULL+0/NULL+4; still 2 flips.
Update (M0.18): that loop was heap corruption caused by a **PPU lifter bug** (a mid-function spill of a callee-saved
register was mistaken for a prologue save, so its reload returned the entry value; 18 functions affected, fixed in
`ppu_lifter.py`, relift required). No NULL reads or heap damage remain. Current stop: the WWS policy-module run of
wid 7 never returns. It loops waiting for DMA tag 5, a stall-and-notify list whose SN interrupt is taken but never
acknowledged; the main thread waits for that workload to go idle.
Update (M0.19): the SN is refused because LS 0 (the PM's interrupt vector) was overwritten by a job's `PUTL`. The
job built its output-list descriptor from a JobApi buffer/tag allocation (PM ~0x16F0) that returned -1, so the list
address became LS 0. Next: why that PM allocation fails for the RenJobSet job (buffer-set binding).
Update (M0.20): the allocation never ran. The job calls the JobApi with `bisl $0,$0` (link register = target
register), and the SPU interpreter wrote the link before reading the target, so the call returned to itself. Fixed in
`spu_interp.c` and, for future lifts, in `spu_lifter.py`. The wid 7 PM run returns and the game reaches **about 740
flips in 48 s** (was 2). Current stop: the flips cease around 48 s and the main thread spins on `0x20A9AE94`
(lr `0x467B2C`, WWS job completion).
Update (M0.21): that spin is the normal per-frame job-list wait; the stops are on the RSX side.
* The Vulkan backend CPU-decoded whole mip chains on every BEGIN, so a single FIFO drain took minutes. The port now
  defaults to `VK_TEX_OFF=1`, which is sound while guest shaders are off.
* The walker's "circling" resync jumped to GoW:A's fixed `put` and discarded frames. The port now defaults to
  `GCM_FIFO_CIRCLE_PASSES=0`.

With both, a run reaches about 1300 to 1600 flips. The remaining stops vary from run to run:
* a JUMP park that is never patched;
* the walker stalled on an ordinary method.

See FEASIBILITY M0.21.
Update (M0.22): the remaining stop is a three-way wait:
* the main thread waits for a batch counter whose last decrement is an RsxSync job;
* that job waits for a GET_REPORT inside the next FIFO segment;
* that segment's start park is released by the main thread only at the end of the frame.

Ruled out by measurement: fence publication, a lost atomic update, and an SPU DMA re-parking a released segment. Next:
who writes the batch-slot owner bytes (`0x99B480+i`) and the frame counter (`0x99B4BC`), and when, compared with the
kick.
Update (M0.23): a stack snapshot of the main thread corrected this. The frame actually waited, in `0x3A5AA4`, for a
two-stage SPU task batch: 5 tasks of the ELF at `0x699000`, whose finalizer creates 5 tasks of the ELF at `0x681880`
SPU-side. Fixes:
* both ELFs lifted (SPU images 15 and 16);
* tasks are no longer dispatched twice (claim under the taskset lock, explicit taskset/id);
* taskset `size` (`0x2900` for Taskset2) and event-flag id fields are set, so the SPU-side `cellSpursCreateTask` works;
* SPU-created tasks are started when the taskset header line is committed (the ELF field flags are masked off).

The batch now completes. Next stop: the main thread in `func_00416A10` (via `0x35E8EC`, frame function `+0x2C4`).
Update (M0.24, in progress): runs now reach 1300-2200 flips. The main-thread stop varies (`0x416338` job scheduler,
`0x375A60` ref throttle), but on the RSX side it is always the same: the walker parks on an EDGE hole that is never
filled. Measured chain:
* the hole's EDGE job is in the EDGE list;
* the EDGE PM waits at a `(2, slot)` entry on the previous slot of the batch-counter ring `0x99AA80` (count 1);
* the MLAA HLE does 5 of that slot's 6 decrements; the last comes from an RsxSync job (LS `0x4618`);
* RsxSync sits on an armed report whose GET_REPORT is not on the walked path before the hole.

Correction: the permanent stops come in three shapes:
* **A:** the post-wrap frame's segment chain holds an invalid segment and overwrites its own header.
* **B:** the EDGE PM is parked on a pre-wait job.
* **C:** the main thread waits for the two-stage task batch in `0x368C64 → 0x3A5AA4`.

Frame segments are 16-byte records (table `*(0x81B9F8)`, atomic count `0x967E60`) written by the PPU, HighestPrio6 and
RenJobSet. They are sorted, then linked at frame function `+0x5CC`. See FEASIBILITY M0.24.
**Shape A fixed:** our HLE `_cellGcmSetFlipCommand` "wrapped" the title's context to `begin` whenever a block was near
its end and `get` looked past `begin + 0x20000`. GoW:A's per-frame 64 KB blocks, which use the title's own callback,
hit that right after the 8 MB ring wraps, so the frame overwrote its own header. The wrap now applies only to
SDK-style contexts (no callback, or ours). Runs now reach game frame ~358 (was ~194). New stop: main thread in
`0x352378`/`0x416A10`.
Update (M0.25): **shape C fixed.** A batch task could run twice: `CreateTask` set the taskset bits before claiming
the task, so a concurrent task exit started it, it finished, and the creator dispatched it again. The count overshot
(`+0x68 = 6` for 5 tasks), and `0x3A5AA4` waits with `!=`. Tasks are now claimed before their bits are set, and runs
reach about 2090 flips. Remaining stop (shape D): the PPU job wait `0x352378 → 0x416A10` on a RenJobSet job. Its
dependency halfword `0x21600684` stays at `0x0013`, the same value as shape B's row, so they are likely one wait.
Update (M0.26): **the varying stops were one runtime bug.** The MFC engine table had 8 slots keyed by context pointer,
and job, task and policy-module contexts never gave theirs back. Right after boot almost every SPU shared one
`tag_completed`. One context's parked stall-and-notify list then left another context's tag "incomplete", a WWS
policy module spun on it, and since the port's SPURS kernel runs an instance's modules on one host thread, every
workload stopped (EDGE holes, shapes B and D). Contexts now release their engine (`spu_coh_unregister`), and the table
has 64 slots. A run went 560 s and 3020 flips with no stall; a 30-minute run never stopped, and the game frame
counter reached 901 in 570 s (~1.5 frames/s). Profile: the single host thread that runs every SPURS policy module
is ~74% busy (BrBDblBufWrkld 30%, EDGE0/1/2 ~11% each), so the next limit is that serial kernel. Frames are still
black (no guest shaders: glslang is not installed).
Update (M0.27): 90-94% of the WWS job time ran in the SPU interpreter, because the job modules the policy module
loads at run time had no lift. The 33 modules embedded in the EBOOT are now carved, lifted at LS `0x4000` (images
200-232) and matched by header, with extra entries for computed-branch targets (`spu/jobmod_extra.json`). Result:
BrBDblBufWrkld went from 35% to 11% of the kernel thread (0% interpreted), and the game runs at ~2.8 frames/s (was
~1.5). Still interpreted: RenderJobs and MeshProcJobs (missing entries), and EDGE code loaded outside the module image
(LS `0x111C0-0x11CF0`).
The limit then turned out to be latency: the port's SPURS kernel slept 1 ms between passes, and each frame is a chain
of dependent jobs across workloads. `SPURS_PASS_GREEDY=1` (a port default now) skips the pause after a productive pass:
**~6.2 game frames/s** (was ~2.9), with the kernel thread ~99% busy. Next levers: lift the EDGE code fragments (11
EBOOT blobs staged at LS `0x11090+`), and a second kernel thread (the "Big" instance has `nSpus=2`).
Update: the EDGE fragments are lifted, one image per (source EA, LS placement), and the interpreter now rejoins lifted
code in the resident job module or fragment, not only in the policy module. Interpreted SPU time fell to ~0.1 s per
20 s, and the game runs at **~8.6 frames/s**. The kernel thread is ~100% busy in lifted code (BrBDblBufWrkld 35%,
EDGE0/1/2 16% each); a second kernel thread is next.
Update: the SPURS kernel now runs two host threads (`SPURS_KERN_THREADS=2`, a port default), with a per-workload
claim that keeps a module on one thread at a time: **~11-12.7 game frames/s** (was ~8.8). The "Big" instance really
has 6 SPUs: the HLE read `_cellSpursAttributeInitialize`'s revision argument as nSpus. Each kernel thread now honours
the per-SPU workload priorities (BrBDblBufWrkld is pinned to SPU 5), but 3-6 threads measured slower than 2. Known: one
rare stall with two threads (once in ~3 h of play, not reproduced since).
Update (M0.28): the lifted SPU code was compiled for plain SSE2, so `shufb` ran as a byte loop and `fma` as a libm
call. Now it builds with `-march=x86-64-v3 -ffp-contract=off` (`SPU_HOST_ARCH`), the quadword byte rotates use
`pshufb`, and `fi` no longer calls `ldexpf`. An EDGE job takes 0.30 ms (was 0.78), and the game runs at **~18 game
frames/s** (was ~11).
Update (M0.29): logging was the next bottleneck. Uncapped per-job and per-task lines (1.3 M lines in 70 s) all
serialised on the stdio lock, and `cellSpursCreateTask2` alone spent ~20% of the main thread in `printf`. These logs
are now capped (`SPURS_TASK_LOG=1` restores the per-task ones), and the port defaults to `PS3_VERBOSE=0`. The game
logic runs at **~27-28 game frames/s** (the original targets 30). The screen is still black: there is no RSX shader
translation without glslang.
Update (M0.30): glslang is installed locally (see BUILDING.md), and guest shaders run on Vulkan
(`PS3RECOMP_VK_GUEST_PROGRAMS=1`). The first real frame is the Santa Monica Studio intro logo. Next: the HDR colour
target (`F_W16Z16Y16X16`, GCM format 11), which the Vulkan engine rejects ("not supported yet").
Update (M0.31/M0.32): format 11 is R32F and is supported; the whole intro renders. After it the 3D frames are black
because the scene's colour pass is never emitted: the HDR target `0x00E10000` that the tonemap reads is only ever the
target of a depth-only prepass, and a full-screen quad 0.93 units in front of the camera hides every mesh and particle.
The texture at `0x20AB0000` is the MLAA copy of the (black) display buffer, not a missing video. Fixed on the way:
fragment depth export (`SV_Depth` from r1.z) and the stencil byte when a D24S8 zeta is read as A8R8G8B8. Next: find on
the SPU/PPU side who should emit the colour pass.
Update (M0.33): that state was the game waiting for its menu video. It imports `libvdec` and calls
`cellVdecQueryAttrEx`/`cellVdecOpenEx`, which were unregistered NIDs; with them forwarded to `QueryAttr`/`Open` the
player opens two MPEG-2 decoders and the frame becomes one full-screen video draw. Pictures are black until a real
decoder is present (FFmpeg 9.0 loaded at run time, `PS3RECOMP_FFMPEG_RUNTIME`). Also added: FP opcodes DDX/DDY and
the PK/UP pack family.
Update (M0.34): FFmpeg 9.0 is installed and the menu video decodes (synthesized PTS, MPEG-2 `frame_rate_code` in
the picture info, which the player paces by). The pictures reach guest memory but no draw composites them yet.
`scripts/play.bat` runs the game in a window.
Update (M0.35/M0.36): the colour pass was missing because EDGE culled every static/colour object. Its triangle
culler (`0xD618` in the geometry module, called only when the job's flags lack `0x800`) ORs masks in with
`fnms rt, mask, 0xFFFFFFFF, rc`, which relies on SPU single precision having no NaN (`0xFFFFFFFF` is a huge
extended-range number, and 0 times it is 0). The float helpers used host IEEE floats, so the lane became NaN, `gb`
saw bit 1, and every triangle was culled. The helpers now follow SPU rules (extended range, denormals are zero,
saturation; the old fast path is kept for ordinary normals). The menu cinematic and the title screen now render.
Update (M0.37): the game reaches its first level. The menu is real-time 3D (EDGE draws it; the decoder is closed),
the intro is a video. `START`@130/150 s, `CROSS`@170 s, then `CROSS` about every 60 s (main menu, the profile save
dialog, the next prompts) loads the Prison of the Damned at about 530-600 s; the real-time intro (Kratos chained, the
rings) renders in full. Fixes on the way:
* The SPU DMA guard that halts a job after 256 identical transfers also halted EDGE jobs that poll the RSX label
  `0x70000C80` for ring space (a healthy wait); it now also needs `SPU_DMA_REPEAT_SECS` (default 30) of wall time.
  This was one rare stall.
* The Vulkan backend had 512 pipeline slots; the level selects about 740 new fragment programs, so every later
  pipeline failed silently and the level was white. Now 8192, with a warning when full.
* With the colour pass back, EDGE runs 360 (fragment, LSA) placements instead of 45; the 315 new ones ran in the
  interpreter (~70% of EDGE time). All are lifted now (EDGE 0% interpreted); the overlay table is 2048 entries with
  an EA hash, since every MFC GET looks itself up.
* A colour target re-bound at the same address and pitch with a smaller clip (GoW:A's 640x360 pass over its
  1280x720 scene) recreated the surface and lost the scene, so the copy-out for the SPU anti-aliasing left a black
  quarter. A same-format, same-pitch surface at least that large is now reused, as depth already was.
Open: the level runs at 1-3 flips/s (wid 8 `BrBDblBufWrkld` takes ~75% of a kernel thread, and the main thread spends
most of its frame in batch-job waits). Menu text is not drawn.
Update (M0.38): the level no longer stops on `BATCHJOB: Waiting for batch job list 1 to finish (22/21 jobs completed)`
(4 of 4 runs before, none in a 1600 s run after). The policy-module emulation handed the wwsjob PM its 64-bit
workload argument with the low half in both word 0 and word 1 (a layout another title's PM needed). GoW:A's PM takes
the job-list header from word 1 and an optional status area from word 0, skipped when zero; with the header in word 0
it wrote its per-SPU status rows at header + 0x130, inside BatchJobList's list 1. A list of exactly 7 jobs then read
a garbage 8th entry (`FFFFFFFF`), counted 22 of 21 and the wait never ended. `SPURS_PM_ARG_BE=1` (port default) passes
the argument big-endian in the preferred doubleword, as the hardware does. The real-time intro now plays on (Megaera,
the chains breaking); its image is overexposed. Open: performance (main thread 68% in the EDGE wait, wid 8 ~80% of a
kernel thread), menu text, the overexposure.
Update (M0.39, performance): a host-side profile (`PS3_SAMPLE_HOST=1` + link map) showed the SPU float helpers
(`fa/fs/fm/fma/fnms/fcgt`) at ~27% of the kernel threads: they were per-lane calls that sent every lane with a zero
operand through doubles. An SSE4.1/FMA vector fast path (exact for ordinary normals and zeros, the reference path for
the rest; 200k-vector differential test) removed them from the profile. The FIFO walker slept its 4 ms tick on every
self-JUMP park; it now yield-polls the park (`GCM_PARK_SPIN_US`), menu/intro 22 -> 27-31 game frames/s. The SPU
decrementer overflowed 231 s after boot (upstream fix 7e257f7 applied). Wid 8 (`BrBDblBufWrkld`) is the audio
DSP rack (`brbProc`), free-running: it takes whatever kernel time is left (60k -> 106k jobs/min with more threads),
so more kernel threads do not raise the level's frame rate. The level still runs at ~1-1.5 game frames/s: every
wwsjob PM run executes about one job and hands the SPU back (the HLE poll answer means "switch to wid 0"), so a
frame of ~550 EDGE jobs per list pays one kernel pass per job. Keeping the module running (poll answer = own wid,
or an immediate re-run) stops the game; not solved yet.
Some stops resolve by themselves. Open question: is the dependency real (through the hole), or is it an
emission/arming order the emulation gets wrong? A second pattern (the EDGE PM parked on a job entry, row `0x00130013`)
is unexplained.

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.13): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
