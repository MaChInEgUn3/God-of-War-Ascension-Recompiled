# Status (updated 2026-10-28)

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
Update (M0.40, toolkit v0.16.0): the port now builds against upstream ps3recomp v0.16.0 (`749182e`) with this
project's patch rebased on it. Upstream's hardware-exact SPU single precision (truncation, extended range) removed the
level's overexposure, which came from our round-to-nearest float model. Upstream also already had our
interpreter `bisl` fix, the policy-module argument layout and an AVX2 fast path for the exact float model. Menu
speed is the same as before once runs start from the same FIOS cache state (an earlier "30 vs 17 game frames/s"
comparison was a warm cache against a cold one). The kernel no longer consumes a workload's readyCount on dispatch
under `SPURS_KEEP_READY=1`, and with it `SPURS_PM_RERUN_US` no longer stops the game, but it does not raise the
level's frame rate either. Generic fixes are being sent upstream as small PRs (first: the Vulkan pipeline table).
Update (M0.41): menu text renders ("Latest News", "Single Player", "Options", build number). The font atlas is an
X16 texture sampled through crossbar 0xAAEE (G and B sources); the runtime presented X16's R16 resource through the
ARGB lanes, where G and B are empty, so every glyph had zero colour and alpha. X16 is now presented like B8 (value in
R, G and B). Upstream PRs open: #378 (Vulkan pipeline table), #379 (cellVdec Ex entry points), #380 (MPEG-2 PTS and
frame rate), #382 (sub-clip surface reuse), #383 (DMA repeat guard wall time), #384 (X16 presentation).
Audio: the game's mix is fine (`AUDIO_PEAK` 0.1-0.37, `AUDIO_WAV` captures the menu music). The WASAPI backend
failed on a device not running 48 kHz stereo float (`0x88890008`); it now asks for shared-mode conversion
(upstream PR #385). A machine with no output device reports `0x80070490` and runs silent, as before.
Update (M0.42): the job modules and EDGE fragments are now lifted with `--merge-chunks` (the lifter otherwise emits
every basic block as its own function, linked by trampolines through the drain loop). Median EDGE time per job in
the level, from `SPURS_PM_PROF` windows where EDGE is busy (`analysis/pmprof_edge.py`): 0.49 ms before, 0.33 ms with
merged job modules, 0.31 ms with merged fragments too. The dispatch helpers (`spu_task_launch_check`, `spu_drain_call`,
`spu_indirect_branch`, `spu_lookup`, `spu_check_stack_reset`, `yz_lockstep_tick`) fell from ~27% to ~10% of all host
samples at t=900 s. A per-thread
memo in front of `spu_lookup` was tried and changed nothing (reverted).
Update (M0.43): the main thread spends most of each level frame at `0x003E7A34`, sleeping 30 us at a time until a
halfword at `0x0099AAC0` clears; the WWS policy module (`gowpm0`, fn `0x2628`) clears it by DMA when the frame's job
list completes. `analysis/frame_wait.py` turns `PS3_POLLTOP` reports into wait and work ms per game frame. With the
audio DSP workload (BrBDblBufWrkld, wid 8) given its own kernel thread on SPU 5 as on hardware
(`SPURS_KERN_SPUS=0,1,5`, opt-in; see below), the wait fell from 458-483 to 389-390 ms per frame and game
frames rose from ~80-85 to ~100 per minute (two runs each). Tried without gain: 4 plain kernel threads (EDGE ran
more jobs, audio took the extra thread), probes compiled out of the SPU LS accessors (43% fewer instructions in a hot
EDGE function, same time per job), inline PPU guest loads (`analysis/patch_ppu_inline_loads.py`, kept: it is what
the current lifter emits).
`SPURS_KEEP_READY=1 SPURS_PM_RERUN_US=2000` on top of that: wait 435 ms per frame (worse) and a crash at ~850 s
(tid 14 calls through a garbage function pointer); dropped.
`SPURS_PASS_SLEEP=0` (no 1 ms nap after an empty kernel pass): wait 425 ms per frame, no gain. The game itself
peaks at ~1.8 GB private (`ws=`/`priv=` now in the heartbeat); a run stopped for low memory was the rest of the
machine, not the game.
Reverted as default: with `SPURS_KERN_SPUS=0,1,5`, 3 of 8 level runs clearly corrupted guest state between 510 and
930 s and 1 is ambiguous (only its frame counter read turned to garbage). One had no other knob (def1 at ~840 s: tid 14 calls through a garbage pointer, EDGE wid 17's policy run sticks, the
frame counter is overwritten, private memory jumps 1.2 GB); the other two had experimental knobs on top. 0 of 8 runs with two kernel threads
corrupted anything. `GCM_PARK_SPIN_US=30000` was tested on three threads and corrupted at 510 s, so it is untested. The port
default is two threads again until the race is found.
Update (M0.44, 2026-10-20): the gain is the third kernel thread, not audio on its own SPU. Per config, level runs
to 960 s that corrupted guest state: `0,1` (default) 0 of 8 (from several builds, not one), `0,1,5` 3 of 8 plus 1
ambiguous, `0,5` 1 of 3, `0,1,2` 2 of 8. `0,5` is as slow as the default (465-468 ms wait per frame); `0,1,2` is
as fast as `0,1,5` (394-431 ms against 389-391), so `SPURS_KERN_SPUS=0,1,2` is the faster opt-in, still with
the race. The guess that SPU 5 was the cause is wrong: under `0,1,2` no thread skips a workload, and it still
corrupted (k012_7 at ~850 s, k012_8 at ~880 s). Every corrupted run shows the same signature: fios thread tid 8
(`fios scheduler 4`) reads through a NULL object field in guest functions `0x004A07C0`/`0x0048810C`, then the
game itself prints `invalid size` and 482 `CopyLocalMemory ... offset error` lines (its own TTY output, offsets
stepping by 16 MB off a garbage size), and wid 1's SPURS state byte reads as garbage. `bctr to NULL from
func_00606898` is in every run, clean or not, and is not part of it.
Two spuNum changes were tried and reverted (the kernel passes spuNum 0 to every policy module run, from every
kernel thread): spuNum = the SPU the thread plays made EDGE wid 15 hang as SPU 5 (pc `0x5ED8`) and stalled the
game ~180 s in 3 of 3 runs; spuNum = the thread index still corrupted 2 of 3 runs. The frame counter overwrite at
`0x009710C8` is not an SPU DMA PUT (`SPU_DMA_EA` over that range logs nothing).
Kernel change (built, effect untested): a thread now skips a workload whose priority on its SPU is 0 (when another
thread may run it) before it touches the workload's readyCount or signal bit. Before, the SPU 5 thread of `0,1,5`
took one unit of wid 0's readyCount on every pass and then skipped it. It changes nothing for `0,1` or `0,1,2`,
where no thread skips.
Update (M0.45, 2026-10-20): **the writer is our own RSX blit, not the game and not a thread race.** A page guard
on `0x009710C8` with `GOW_GUARD_MAX=100000` caught the overwrite in both guarded runs that corrupted (grd_1,
grd_3, 2 of 4 under `0,1,2`): the store comes from the FIFO walker's thread, `nv3089_blit` -> `gcm_row_copy`
(memmove), named through the link map (`MyGameRecomp.map`). The NV3089 scaled-image blit resolves a VRAM offset as
`localAddress + offset` in 32 bits, so an offset past the end of VRAM wraps into main memory; real RSX raises a DMA
fault instead. That is the texture-looking garbage (DXT-like words) found over the frame counter, a FIOS object and
the FIFO ring, and why no PPU store or SPU DMA was ever seen writing there. The game itself is already confused a
few hundred log lines earlier (its own `invalid size` / `CopyLocalMemory ... offset error`), so the blit is at
least the amplifier; whether an earlier out-of-VRAM blit is also the origin is being measured (runs oob_1..4, blits
that leave VRAM are now skipped and logged, `GCM_BLIT_OOB=1` restores them).
Also settled: corruption is not specific to three kernel threads. Two-thread runs that live long enough also fail
(long2_3 hung at game frame 8447; earlier def1/snidx2 were two-thread), and the failures cluster at game frames
~7800-8750, a point in the level. The FIOS seek/read interleave idea is falsified (a per-thread seek-position check
found 0 mismatches and runs with the position pinned still corrupted). The `FIFO JUMP/CALL to unmapped IO` seen in
every corrupted run is a victim: it always comes after the game's first error, and the bad word is still in the ring
when dumped. A different, benign-looking case is a torn read: the walker follows a park released by an EDGE PUT
whose first word lands before the rest (long2_3 read `0x3FBF7887`, the dump right after showed `0x00200A20`); the
walker now re-reads such a word for up to `GCM_SETTLE_MS` (default 2) before resyncing. GCM map/unmap activity is
not involved: the same `MapMainMemory` calls happen at level load in clean and corrupted runs alike.
Update (M0.46, 2026-10-20, in progress): **the blit is downstream; the first symptom is a bad size in the game's
VRAM-defrag copy queue.** In fen_2 the out-of-VRAM skip fired for the first time, but only after the game's
`invalid size`, so skipping the blit does not stop the corruption. A fence/barrier MFC command overtaking a stalled
DMA list in the same tag was tested and ruled out (detector: 0 hits in three runs, one of them corrupted).
`TTY_BT` on the game's own message (tty_2) shows it comes from the title's CopyLocalMemory (`0x374A80`, rejects
sizes >= 512 MB), called from the defrag queue drain (`0x373690`, queue at `0x96E710`). In both corrupted runs only
the entry's size word is garbage; its dst/src are valid, aligned VRAM offsets, so the entry was written by the game
from a bad source value rather than scribbled over. Push and drain both run on the main thread (no cross-thread
race there). An overflow of the 32-entry pending-move list was ruled out for clean runs (queue peak 9, pending list
used twice in 25 min). Next: the allocator dump at the first `invalid size` (`TTY_BT_DUMP`) to find which
descriptor carries the bad size. Separately, qp_2 hung at the end of the intro movie (main thread waiting at
`0x3E84EC` in `0x3C6450`); not yet analysed. Runs now reach the level in ~160 s (intro skipped by CROSS).
Update (M0.46b, 2026-10-21): **the negative size is written by the game's own release path, for an object whose
descriptor pointer does not match it.** A per-drain bounds check of the allocator's block descriptors (0x468
records of 0x80 bytes at `0x2154B4C0`: +8 offset, +0xC size) showed a block whose size goes negative just before
the first `invalid size` (dg_1, block #894, `0xFFFDAAB0`). A multi-page guard on the whole array with a
negative-field check (`GOW_GUARD_LEN`, `GOW_GUARD_NEG=80,C`) caught the write in nga_4: lifted code
`func_00370E48` (tid 1), called by the compactor `func_00372B70+0x950`, stores `desc.size - obj[+0x1C]` into the
descriptor `obj[+0x44]` points at: `0x5560 - 0x50000 = 0xFFFB5560`. The same 0x50000 subtraction against a
different small block explains dg_1 (`0x2AAB0 - 0x50000`). So one class of 0x50000-byte resources is released
against a descriptor that belongs to a smaller block: either the descriptor was reassigned under the object
(stale pointer) or the object is released twice. Next: trace the release with the object's fields to tell the
two apart, then find why the compactor selected that object. Reporting 249 MB of local memory
(`GCM_LOCAL_SIZE=0F900000`, like a console) does not prevent it (ls249_8 corrupted). Everything on this path so
far is main-thread lifted PPU code; the FIFO torn-read track is separate.
Update (M0.46c, 2026-10-21): captured with the release trace (rba_2). The object released was `0x26128260`
(`+0x1C` 0x28000, `+0x20` 0x2AAB0) and its descriptor #907 held **0x2AB0 = obj+0x20 - obj+0x1C**, the part
without the top mips; the compactor (`0x372B70`, LRU pick under memory pressure) released the full `+0x1C`
against it, and then released the same object again 50+ times in a row, each pass subtracting 0x28000 again
(healthy releases happen once). nga_4 fits the same arithmetic (`0x5560 = 0x55560 - 0x50000`). No rebind
(`0x36E804`) ran in that run, so "a rebind copied the resident flag" is out; the leading reading is that the
LRU site at `0x3734BC` releases an object whose descriptor has nothing resident (the game's other release site
`func_00371004` checks `desc+0x20 != 0` first, this one does not), so nothing is freed and the pressure loop
picks it again. Being checked with a dump of the descriptor at each release
(`PPU_TRACE_DUMP=370E48,4+0x44,30,1`). Also found and fixed on the way (toolkit 9a7138c): our `sys_lwcond_create`
HLE wrote a word past the 8-byte `sys_lwcond_t`, clobbering the field after every lwcond (the sound-stream
callback slot in GoW:A: all the `bctr to NULL from func_00606898` lines). It did not affect the corruption.
Static notes on the defragmenter: `analysis/vram_defrag_re.md`.
Update (M0.46d/M0.47, 2026-10-21): the release with nothing resident is confirmed (rcb_6: `desc+0x20 == 0` at the
first bad release; 0 of ~900 healthy releases have it). Mitigation: `port/gow_vram_guard.c` (inserted by
`scripts/patch_lift.py`, on by default, `GOW_RELEASE_GUARD=0` off) skips such a release and drops the object from the
LRU. A/B over complete 18-minute runs: guard on 0 corrupted of 9 (vetoes fired in 2), guard off 1 of 8; a 224 MB
local-memory stress run did not raise the rate. **Toolkit base moved to ps3recomp v0.17.0**, which contains our eight
earlier PRs. v0.17.0 as released ran GoW:A several times slower: `mfc_for()` called `getenv("SPU_PCSAMPLE")` on every
MFC channel op while it was unset (Windows CRT environment lock), putting SPU policy modules at 74% time in channel
writes (v0.16: 21%). Fixed in our patch and sent upstream (PR #442, closed as a duplicate of the earlier PR #243 with the same fix); with it v0.17 matches v0.16 in the level
(game frame ~2520-2820 at 1080 s vs ~2580) and three runs were clean. A player launcher is in `launcher/`.
Update (M0.48, 2026-10-21): performance profile of the level (solo run, host samples after 300 s). Each of the two
SPU kernel threads spends ~60% in lifted job code, 16-21% in dispatch (`spu_indirect_branch`, `spu_lookup`,
`spu_task_launch_check`, `spu_drain_call`) and ~10% in debug helpers; the PPU main thread spent ~46% of its samples in
`barrier_watch_hit`, the write-watch walk every guest store made with every watch off. Two overhead cuts, both
neutral-or-better and within run-to-run noise (game frame at 1080 s 2001/2961 vs 2375/2864 base, no corruption):
an inline gate in front of the write watches (`PPU_WDEC`/`PPU_WJUMPBACK`/`PPU_WVAL`/`PPU_WWATCH` are read once), and
`/Zc:tlsGuards-` in `port/CMakeLists.txt` (clang-cl put an on-demand TLS init check, plus a second TLS lookup, on the
trampoline store of every lifted SPU and PPU step; nothing in the tree has a dynamically initialized thread_local).
Three SPU kernel threads (`SPURS_KERN_SPUS=0,1,2`) still hang: the EDGE policy module (wid 17) stayed in one run for
588 s polling for something never delivered (its DMA tags complete) while the game froze. Two kernel threads stay the
default. One run of the new build froze early (game frame 315, all workloads idle, main waiting in `func_003C6450`
for a job list), like an earlier intro hang; the other two passed that point.
Update (M0.49, 2026-10-21 night): **the random freezes are an RSX FIFO park race, fixed.** An EDGE job releases the
walker's park (a self-JUMP) with the same SPU PUT that carries the new segment, park word first; the walker followed
the release while the rest of the PUT was still landing (valid to +0x24, stale data at +0x28), decoded junk, met a
JUMP to unmapped IO and stopped. The RSX ring-space label (0x70000C80) then never advanced and the EDGE job polling it
(pc 0xC834, overlay 225) held its SPU forever. It killed 3 of 12 baseline runs (including the frame-315 freeze above
and the 3-thread hang) and every faster configuration. `SPU_PUT_HEAD_LAST=1` (now a port default) stores the PUT's head
last behind a store fence: 8 runs with it, 0 torn reads, 0 freezes. Faster dispatch tried: re-running a productive
module at once (`SPURS_PM_RERUN_US=2000`) gave about +8% frames but 2 of 4 runs hit `invalid size` with no guard veto,
so it stays off; the VRAM guard now also refuses any release that would drive a block size negative
(`[vram-guard-underflow]`). Answering selectWorkload polls with the module's own wid (`SPURS_POLL_SELECT=1`) deadlocks
on our synchronous 2-thread kernel: the module keeps one of the two threads while the producers of its list cannot
run. Upstream v0.17's own scheduler (one coordinator, one persistent worker per SPU with its real SPU number, admission
by maximum contention) avoids that; our rebase kept our older synchronous kernel instead. Moving to that model, with
the audio workload alone on SPU 5, is the next step. The audio workload (wid 8) is real work: 8 jobs per audio block,
~0.5 ms each, ~80% of one kernel thread.
Update (M0.50, 2026-10-22): **the VRAM guard was silently off in about half the runs.** The game puts its VRAM
descriptor table at 0x2154B4C0 in some runs and at 0x8154B4C0 in others; `gow_vram_guard.c` returned early for
addresses at or above 0x40000000, so in the high-range runs nothing was checked (that is why `invalid size` appeared
with no guard line under the faster dispatch). The range check is gone (the whole 32-bit guest space is backed). With
it, 8 runs of the re-run dispatch had 30 vetoes, 0 `invalid size`, 0 freezes (before: 3 corrupted of 7). The
re-run dispatch itself did not measurably beat the default once level entry is controlled for
(`analysis/level_rate.py`: game frames per second over 540 s from 60 s after entering the level; base ~1.06, park fix
~1.13, park fix + re-run ~1.13, with runs clustering at ~1.0 or ~1.2 by scene), so it stays off. The
keep-running poll answer is mapped but not solved: once answered "keep running", GoW:A's WWS manager reaches its
pipeline loop at LS 0x1708 (waiting for the store stage, LS 0x12F0) with job 0 marked running that never completes,
and that loop has no poll, so no later answer can reach it.
Narrowed the same night: under keep-running the WWS manager pipelines jobs, and the EDGE geometry job (module
d93503647dd8, call at LS 0xD1E0) then calls an EDGE code fragment staged at a placement the default dispatch never
uses (0x1D1A0, code present). No lift exists there and that dispatch path has no interpreter fallback
(`SPU_INTERP_UNLIFTED` covers a different site), so the job stops mid-run and the manager waits for it forever.
In one run the saved LS also showed ~363 frames of 0x180 bytes (an EDGE fragment's frame size) stacked inside
the job down into the EDGE buffers; the policy-side stack pointer stays constant between jobs, so these build up
inside one job, likely after the aborted calls. Next: an interpreter fallback at that dispatch site, or lifting
the second set of fragment placements from keep-running logs, then re-test the keep-running answer.
Some stops resolve by themselves. Open question: is the dependency real (through the hole), or is it an
emission/arming order the emulation gets wrong? A second pattern (the EDGE PM parked on a job entry, row `0x00130013`)
is unexplained.

Update (M0.51, 2026-10-22): **the interpreter fallback now also covers that dispatch site** (toolkit: with
`SPU_INTERP_UNLIFTED=1`, a branch to an address with no lift, after the self-modifying-code microstep fails, runs the
live LS code in the interpreter and logs `interpreting unlifted LS ...`). Under keep-running the 0x1D1A0 fragment
is now interpreted instead of aborting the job, yet the run still stops at frame ~164 with the EDGE workload
(wid 15) in the same pipeline loop, so the missing lift was a symptom, not the cause. `[wwsapi]`/`[wwsjob]` also
log r1 now (after the first 60 lines per workload, only calls whose r1 moved). Keep-running stays off and open.
**The audio workload now has its own kernel thread by default** (`SPURS_KERN_SPUS=0,1,5`, port `main.cpp`). It was
opt-in because 3 of 8 runs with it corrupted guest state; those turned out to be the FIFO park race and the
off-in-high-range VRAM guard, both fixed above. With them fixed: 7 runs of 1080 s, 0 corrupted, 0 freezes,
level rate 1.18-1.25 vs 1.10-1.18 game frames/s with two threads (~+6%), with the order holding when the two run
instances are swapped. The faster configuration fires the VRAM guard veto in more runs (7/7 vs 3/7), with 0
`invalid size`: the mitigation is doing its job. `SPURS_KERN_SPUS=0,1` goes back to two threads.
Per-job dispatch cost, first pass (profile with the new default, 3396 samples from 250 s): on the EDGE kernel
thread ~21% is dispatch (`spu_indirect_branch` 7, `spu_lookup` 7, `spu_task_launch_check` 4, `spu_drain_call` 3);
on the PPU main thread `ppu_trace_enter` is 5% (the lift instruments all 21k guest functions). Toolkit now caches
`spu_lookup` results per thread (misses too, invalidated by new registrations) and rejects in `ppu_trace_enter`
before any other work; the port defaults `PPU_TRACE_ADDRS=0` (without it, 4000 `[FN]` lines per boot). Measured
neutral: new 1.18-1.20 vs old 1.14-1.19 on one instance, 1.12-1.18 vs 1.15-1.17 swapped (+1% pooled, inside the
noise), 12 runs, 0 corrupted. Kept as clean-ups. Instance A runs ~2% faster than B, so A/B pairs must swap.

Update (M0.52, 2026-10-22): **the frame waits on the GPU, one draw at a time.** The chain, measured in the
level:
- The main thread waits ~620 ms per game frame for the WWS job lists and works ~500 (`frame_wait.py`).
- The three kernel threads are only 217% busy of 300 (`[pm-prof]`); the EDGE workloads (wids 15-17, ~1670 jobs
  per game frame, ~0.58 ms each) spend 15-27% of their time reading the decrementer (`[pm-prof-ch]`: ch8).
- That read is a backoff: `jobmod_d93503647dd8` function 0xB700 polls a label in main memory by DMA (the RSX
  ring-space label) and, while there is no room, spins `rdch $ch8` 2400 times (`SPU_DECPC`). Making the read cheap
  (TSC instead of QPC plus divides) only made it poll more often; the share went up, the wait stayed.
- The ring frees as the FIFO walker passes its semaphores, and a `PS3_FTRACE` timeline (the Vulkan backend now
  logs each synchronous submit as a GPU_WAIT span) shows the walker thread blocked on the GPU 66% of the time:
  `vk_eng_draw` submits and waits on a fence for **every draw** (54,759 draws in 20 s, 168 us each, 46% of the
  thread), `vk_eng_depth_snapshot` another 11% (375 x 5.8 ms).
This also explains why the re-run dispatch gave +65% EDGE job entries per minute and 0% frames: more SPU work
cannot help while the ring drains at one fenced draw at a time. Next: batch draws into one command buffer
(per-draw vertex/index/uniform ring slices and descriptor sets, flush before any other backend operation).

Update (M0.53, 2026-10-22): **draw batching, on by default.** `VK_DRAW_BATCH=1` records engine draws into one
command buffer: each draw takes slices of three ring buffers (vertices 64 MB, indices 16 MB, constants 32 MB) and a
descriptor set from a pool, with a full memory barrier between draws in place of hazard tracking. Up to
`VK_DRAW_BATCH_MAX` (default 512) draws go per submission. `vk_begin`, every object destroy, the present and the
engine's own submit point flush first, so all other backend work still sees everything before it complete.
- Timeline (20 s in the level): GPU wait 13.1 s → 8.4 s; the sync-submit wait per flip 228 → 97 ms; flip median
  264 → 190 ms.
- A/B, same exe, instances swapped halfway. In the two pairs with matched level entries (instance B, the slower
  one): 1.593 vs 1.141 and 2.089 vs 1.852 game frames/s. The other two pairs entered at different times, so they
  compare different scenes, but they had the same order. Absolute numbers vary with the scene: 2.09 with batching
  off in one run is scene, not code.
- 8 runs: no hang, no `invalid size`, no BRANCH-TO-0. The vram-guard trips are the same "release skipped" kind with
  batching on or off. Some frame dumps show texture garbage, with batching on or off (see the open bug below).
Update (M0.54, 2026-10-22): **the rest of the per-operation waits go into the batch, and the GPU stops being
the limit.** Depth snapshots copy through a persistent device-local buffer that is recorded into the open batch.
Texture uploads stage through a 64 MB ring and record a buffer-to-image copy. Colour and depth/stencil clears
record too.
- GPU wait per 20 s in the level: 8.4 s with draws batched, 5.6 s with snapshots and uploads batched, 2.9 s with
  clears batched. What is left is readbacks (~0.6k x 2.8 ms) and the present, which need the result.
- Snapshots and uploads, A/B against draw batching alone (all 8 runs entered at 240 s): 2.097 vs 1.974 mean game
  frames/s (+6%). They won 3 of 4 pairs; one pair was reversed.
- Clears, A/B on the three pairs with matched entries: 1.394 vs 1.372, 1.620 vs 1.596 and 1.619 vs 1.602
  (+1.5%).
- Halving the GPU wait again moved frames by ~1.5%, so the frame is now bound elsewhere (the CPU side: SPU jobs and
  the main thread). No hang, `invalid size` or BRANCH-TO-0 in 16 runs. The texture garbage below appears at the
  same rate with and without these changes.
Update (M0.55, 2026-10-22): **a steadier metric, and the batching results re-scored.**
- The old window [entry+60, entry+600] sometimes still held part of the loading screen. The loading screen counts
  game frames at ~17/s and gameplay at ~1-2.5/s, so those runs read high. That explains spreads like 2.09 vs 1.14
  between runs of the same build.
- `analysis/level_rate.py` now also prints `play` (from the end of the loading screen) and `tail` ([600 s,
  1080 s], gameplay in every run). Tail per pair, with instances swapped:
  - draw batching vs off: 1.03/1.15/1.23/1.19 vs 0.74/0.79/0.89/0.89 (+39%, 4 of 4).
  - snapshots+uploads vs draws only: 0.90/1.29/1.27/1.32 vs 0.89/1.18/1.17/1.19 (+8%, 4 of 4).
  - clears batched vs not: 0.87/0.94/0.84/1.27 vs 0.85/0.93/0.85/1.26 (+0.7%, noise).
  - the measuring profilers (`SPURS_PM_PROF`, `GOW_MAIN_PROF`, `PS3_POLLTOP`) off vs on: 1.42/1.36 vs 1.40/1.34
    (+1.6%). Measurements taken with them on are not skewed by more than that.
- Decrementer clock: the TSC path gains ~45 us a minute on QPC (0.75 ppm), so it does not distort game timing.
  `SPU_HOST_NS=qpc` switches back for A/B. The two QPC runs did not reach the part of the level where the texture
  garbage usually appears, so they say nothing about it either way.
- Where the level is now: ~1-1.4 game frames/s late in the level (the boss fight), ~1.6 right after loading. The GPU
  wait is no longer the limit; the CPU side is (SPU job cost and how many host threads run SPU work).
Update (M0.56, 2026-10-22): **in gameplay, two chains of about the same length.**
Measured from a `PS3_FTRACE` timeline at 700-720 s, where a frame is ~500 ms:
- **Walker chain.** The RSX walker is busy ("drain") ~400 ms per frame and never waits on a semaphore ACQUIRE.
  ~170 ms of that is waiting on the GPU at batch flushes: `vk_eng_submit_and_wait` (315 x 10 ms) and `vk_begin`
  (146 x 11.7 ms). The submit-and-waits come from coherence readbacks (a surface read back into guest memory) and
  wait for the whole open batch.
- **SPU / main chain.** The main thread waits ~353 ms per frame for the WWS job lists (`0x3E8388`) and ~32 ms in
  the game's frame pacing.
  - The pacing is `0x375A60`: `usleep(30)` until the flip counter at `*(0x9BE004)+8` reaches the game frame
    counter (`0x9710C8`) − 1.
  - The kernel threads are not saturated. Audio (wid 8) keeps one thread 81% busy. The other two are ~73% busy,
    with EDGE (wids 15-17, ~1500 jobs per game frame) taking ~85% of a thread's worth.
  - EDGE ch8 is down to 3-5%, so the ring-space backoff is mostly gone. The job lists are bound by the latency
    of the job chain, not by thread count.
- `VK_ASYNC=1`: batches are submitted without waiting, with up to 3 in flight. The GPU sync wait went from 199 to
  81 ms per frame, but frames did not move (swapped 2+2, tail 0.918 vs 0.917): the SPU chain is as long as the
  walker's. It stays off by default.
- `PS3_FTRACE` now records SPU busy spans for SPURS module runs, tagged with kernel thread, image and workload. In a
  heavy scene the SPUs are 232% busy of 300%: audio is 34% of that, EDGE 42% (~800 ms of SPU work per ~835 ms
  frame).

Update (M0.57, 2026-10-22): **native SPU single-precision floats** (`SPU_FP_NATIVE`, port build option
`GOW_SPU_FP_NATIVE`, on).
- The lifted SPU code emulated `fa`/`fs`/`fm`/`fma`/`fms`/`fnms` bit for bit, through doubles, with checks. The SPU
  rounds singles toward zero, so with the host's MXCSR set to round toward zero (every `spu_run_with_halt`) a
  native add, multiply or FMA3 gives the same bits for ordinary operands. Other lanes still take the exact path,
  and double-precision ops put round-to-nearest back around themselves.
- `SPU_FP_CHECK=1` compares every op with the exact path: 1.34e9 ops through boot and gameplay, 0 mismatches.
- Swapped 2+2 A/B, tail: 1.175/1.269/1.098/1.031 vs 1.169/1.242/1.042/0.998 (+2.7%, 3 of 4 pairs). EDGE per-job cost
  went down 5-20% (`[pm-prof]`).
- Frames moved much less than the job cost. Per frame, the EDGE work (wids 15-17, `[pm-prof]`) divided over the two
  non-audio kernel threads is 283-375 ms. The main thread's wait on the job lists is 300-398 ms. Those track
  each other in six runs, so the main thread waits on EDGE throughput over two host threads.
- A fourth kernel thread with audio still on its own (`SPURS_KERN_SPUS=0,1,2,5`, swapped 2+2) is neutral: play
  1.096/1.192/1.122/1.103 vs 1.110/1.144/1.190 (plus an outlier at 0.688), with no hangs. Total kernel-thread busy
  only went from ~200% to ~210%.
- Why: the per-thread SPU timeline (`PS3_FTRACE`, 20 s at 700 s) shows **the three EDGE workloads (EDGE0-2, wids
  15-17) never run at the same time (0.0% of the time)**. One kernel thread runs 12 s of EDGE per 20 s. The other
  non-audio thread is idle 59% of the time, 48% of it while EDGE runs elsewhere. The game splits geometry into three
  workloads (each registered with maxContention 1) so they run side by side on three SPUs; here they run one after
  another.
- `SPURS_EDGE_DIAG` confirms it: at every EDGE dispatch (~155k a minute) no other EDGE wid is busy, and none
  has readyCount up. The EDGE runs come in bursts on one thread (1.9 us between runs). Thread hand-offs happen
  only ~1300 times in 20 s, and the EDGE lists are busy 15.9 s of every 20.
- `SPURS_MAXCONT=15=2,16=2,17=2` (two lanes per EDGE list) is slower: tail 0.785 vs 1.402, because a host thread
  is created per lane per dispatch.

Update (M0.58, 2026-10-23): **three defaults that pay together.** Each A/B is swapped 2+2, scored on tail, and
stacked on the previous one.
- `SPURS_PM_RERUN_US=2000` (a module run that executed jobs is re-entered at once, for up to 2 ms): 1.494/1.390/
  1.402/1.419 vs 1.392/1.271/1.294/1.298 (+8.5%, 4 of 4). EDGE jobs per run 0.88 -> 0.97.
  - The WWS policy module hands the SPU back after each job, so a list had advanced one job per kernel pass.
  - At M0.52 this setting gave 0% in frames, while the walker was 66% GPU-blocked and EDGE waited on ring space.
- `SPURS_KERN_SPUS=0,1,2,5` (a fourth kernel thread, SPU 2: EDGE0-2 have priority 8 on SPUs 0-3): 1.542/1.467/
  1.508/1.479 vs 1.429/1.371/1.402/1.400 (+7%, 4 of 4). Without RERUN it had been neutral.
- `VK_ASYNC=1`: 1.590/1.523/1.525/1.512 vs 1.502/1.435/1.456/1.415 (+5.9%, 4 of 4). Alone it had been neutral; it
  pays now that the SPU chain is shorter than the walker's.
- Together: tail ~1.31 -> ~1.54 game frames/s (+17%). 16 runs with no hang, no `invalid size`, no BRANCH-TO-0.
  The texture garbage below shows at the same rate in both arms.
- The port sets all three (`main.cpp`). Each can still be set back by its variable.
- Next: the walker's per-draw full barrier (`vk_cmd_after_everything`, ~60 us of GPU per draw) is the next GPU-side
  cost, now that the GPU sometimes runs three batches behind. On the SPU side, EDGE still runs one list at a time.

Update (M0.59, 2026-10-23): **timer resolution, upstream fixes, and an intermittent level hang.**
- Windows 11 ignores `timeBeginPeriod(1)` for a process with no visible window. Offscreen, each 1 ms pause of the
  SPURS kernel threads took ~15 ms (PS3_FTRACE: 12-16 ms per pause, 22-75% of a thread's time).
  - The port now opts out of that throttling (`SetProcessInformation(ProcessPowerThrottling)`); `GOW_TIMER_FIX=0`
    skips it.
  - Pauses dropped to ~2 ms and the level is reached at 120-180 s instead of ~240 s.
  - Steady gameplay barely moved: tail 1.569 vs 1.544 in the one clean pair.
- Taken from open upstream PRs: #492 (`event_status` updated atomically), #491 (`SPU_IRQWAIT_DIAG`) and #522 (PPU
  reservation re-checked under the lock-line lock). The triage of all 116 open upstream PRs is a local working
  note, not in this repo.
- An intermittent hang in the level (fixed in M0.59e below): an EDGE job spins in its ring-space backoff
  (`pc=0xC834`, `r3=0x960`) after the RSX walker stops flipping, then `[pm-hang]` repeats.
- Also from upstream: #533 (the SPU drain's per-hop hooks run their common case inline) and #534 (guest stores
  test the SPU reserved-line bitmap inline). A/B against the build without them, swapped 2+2: tail 1.508/1.510/
  1.656/1.540 vs 1.417/1.477/1.579/1.515 (+3.8%, 4 of 4), with no hang or bad size in 8 runs.
- Also from upstream: the draw-engine CPU series #458 (vertex fetch attribute by attribute), #465 (index runs in one
  pass), #432 (linear A8R8G8B8 decode), #438 (texture key location cache), #439 (no full hash for engine-rewritten
  textures), and #500 (SPU estimates, interpolate and float/int conversions in AVX2, rounding-mode independent).
  Swapped A/B, stopped after two pairs that agreed: tail 1.379/1.331 vs 1.308/1.248 (+5.4%, +6.7%), no hang.
- Also from upstream: #512 (SSE4.1/AVX2 forms of the SPU per-lane integer and quadword helpers; our SSE quadword
  forms kept). Three clean pairs: tail 1.400/1.273/1.350 vs 1.356/1.252/1.294 (+3.2%, +1.7%, +4.3%). The fourth
  pair hung in both arms at the same scene.

Update (M0.59e, 2026-10-23 night): **the level hang was the Vulkan driver's memory calls; fixed.**
- The watchdog (`GOW_CLOCK_WATCH=20`) showed the walker on a `SET_BEGIN_END(0)`, with put far ahead and flips still
  coming every ~5 s. It was not waiting on a semaphore, a self-JUMP park or a GPU fence (`vk_fence_waiting=0`), so it
  was crawling through the draw engine's END.
- `ENG_PHASE_STATS=<s>` (toolkit) splits the engine's time per window by phase and by texture path. During a hang,
  single texture binds took 15-24 s, all inside depth/colour readbacks to guest memory and texture re-uploads.
- `VK_SLOW_MS=<ms>` (toolkit) times the driver calls. `vkFreeMemory` took up to 2.35 s, `vkAllocateMemory` 1.5 s
  and `vkQueueSubmit` 1.6 s, none of them fence waits.
  - Each readback allocated, mapped and freed a host buffer.
  - Each changed texture was re-created (an allocation and a synchronous layout submit). The old one was then
    destroyed, which waits for every batch in flight and frees.
- Not the same scene, as M0.59 assumed: the two arms of a pair hung at different times (120 s vs 270 s, 450 s vs
  510 s). They share one GPU and its kernel memory manager, which is how one instance's stall reaches the other.
  VRAM peaked at 3.3 of 8 GB (`nvidia-smi`), so capacity is not the cause; the volume of calls is.
- Fix (toolkit):
  - Readbacks and unbatched uploads reuse one grow-only staging buffer each.
  - A changed 2D texture is rewritten in place when the backend orders uploads after recorded draws
    (`rsx_draw_backend.texture_upload_ordered`, set by Vulkan), with the same mip count; otherwise create-and-swap
    as before.
- Hangs: 0 of 4 pairs with the fix (8 runs), against 4 of the 5 pairs before it.
- Walker time per 10 s of level (one pair each):

  | | before | after |
  |---|---|---|
  | texture binding | 5.3 s | 1.4 s |
  | all ENDs | 7.7 s | 4.1 s |
  | re-upload | 29 ms per call | 2.7 ms |
  | depth to guest | 47 ms per call | 8 ms |

  Frames/s, swapped A/B against runL, the last exe before #512 and this fix (no diagnostics on): tail 1.738/1.777 vs
  1.571/1.631. That is +10.6% and +9.0%; stopped after two agreeing pairs, with no hang in either arm. #512 accounts
  for about 3% of it (M0.59d), so this fix is worth roughly +6-7%. Level entry also came sooner: 120-180 s vs
  150-210 s.
  EDGE's wait for RSX ring space (`[pm-prof-ch] ch8`, wids 15-17, after 600 s) fell from ~13% of its time (x5, x9)
  to 1-7% (x12, x13).
- Still slow: `vkQueueSubmit` (up to 1.8 s, ~80 per run) and image frees from evictions and target re-creation.
  Every texture still has its own `VkDeviceMemory`; sub-allocating from large blocks is the next candidate.
- No texture garbage in the dumps of these runs (8-px chroma detector, all below 0.012).

Update (M0.60, 2026-10-24): **what bounds the frame now: the EDGE chain, one job at a time.**
- `PS3_FTRACE` over 20 s of the level (600-620 s, runP, one instance): frame median 463 ms. The main thread
  sleeps 310 ms per frame at `0x3E8388`, waiting for WWS job lists. The walker drains 344 ms per frame. GPU waits
  are down to 34 ms.
- The busiest SPU (93%) runs wid 8, `Wws_Job/BrBDblBufWrkld`, almost alone. That is the game's Boomrang audio
  engine (`src/common/boomrang/api_brb.cpp`, Scream plugin packs), which the title pins to SPU 5 (`priority per
  SPU: 0 0 0 0 0 1 0 0`). It is real-time audio, not per-frame work, so it is not on the frame's path.
- The EDGE workloads (wids 15-17, `Wws_Job/EDGE0-2`) are:
  - busy for 96% of wall time (union of their spans);
  - overlapping each other for 15 ms in 20 s;
  - active during 98.8% of the main thread's waits.

  So the frame is the EDGE chain, and it runs serially. Each wid runs ~1100 jobs/s, with a median span of
  0.18 ms (`pmprof_edge.py`: 0.29 ms per job). No run is idle (`z0%`).
- Serial by design, most likely. `SPU_ATOM_WIDS=<s>` (toolkit) lists the lock lines each workload's atomics touch.
  Each EDGE wid has its own line (its `AddWorkloadWA` data). All three also share `0x83257500`: a ring allocator
  (a write pointer in `0x8326xxxx`, and one in the `0x81966300 + 0x60000` command ring). They update it with
  GETLLAR + PUTLLUC: a read-modify-write with no conditional store, which is only correct if one EDGE runs at a
  time. Checking `wklCurrentContention` on a real PS3 (HEN) would settle it.
- The lever is the cost per EDGE job.
  - Host samples on the EDGE threads: roughly 20% runtime dispatch (`spu_indirect_branch`, `spu_lookup`,
    `spu_drain_call`, `spu_step`). The rest is lifted code, hottest `jobmod_d935...` `E550`/`73E0`/`A880` and
    `jobmod_a67e...` `4AB0`.
  - The lifted code keeps every GPR in `ctx->gpr[]` in memory. Each LS store, a char-type `memcpy`, forces `r1` and
    the LS pointer to be reloaded. Each LS write also tests two global watch flags and the LBP-only `0x1320`
    probe.
  - Next: compile those probes out of the lifted SPU code, and get a fuller profile. Keeping GPRs in locals (lifter)
    is the larger lever and needs a spike first.

Update (M0.61, 2026-10-24): **LS probes out of the lifted SPU code: +12-13% frames/s.**
- The toolkit's `SPU_LS_PROBES` (default 1) gates the probes and the watch checks in
  `spu_ls_read128`/`spu_ls_write128`. The port builds its lifted SPU sources with `SPU_LS_PROBES=0` (CMake
  `GOW_SPU_LS_PROBES`, default OFF).
- An LS store in the hot EDGE function went from ~17 host instructions to 7. Three things went away: the LBP-only
  `0x1320` probe and two global flags reloaded after every store. The `pshufb` mask now stays in a register.
- Swapped A/B against the build with probes (runR vs runQ), both pairs agreeing, no hang:

  | | with probes | without |
  |---|---|---|
  | tail, pair 1 | 1.796 | 2.017 (+12.3%) |
  | tail, pair 2 | 1.831 | 2.069 (+13.0%) |
  | EDGE ms/job, pair 1 | 0.343 | 0.302 (-12%) |
  | EDGE ms/job, pair 2 | 0.329 | 0.295 (-10%) |

  The level tail is now above 2 game frames/s.
- The host sampler took one round per ~430 ms, a system-wide thread snapshot each time, so profiles held a few
  hundred samples per thread. It now keeps thread handles. `PS3_SAMPLE_RANKS` raises the RVA buckets per report.
- `SPU_FP_CHECK_HOOK=0`: the `SPU_FP_CHECK` call after every native `fa`/`fm`/`fma` is never taken, but it is an
  outside call, so registers held in `ctx` were reloaded after each float op. The port turns it off with the LS
  probes (same CMake option).
  - In the three hottest EDGE functions: calls -45-50%, host instructions -17-23%.
  - A/B runS vs runR, 4 swapped pairs, all positive, no hang: tail +3.9/+4.8/+4.7/+4.8%; EDGE ms/job -2.5% to
    -5.4%.
  - The fourth pair finished on its own after the batch script was stopped for low memory; its timeouts were still
    running.
- Profile with the fixed sampler (~43k samples per 10 s): on the EDGE threads ~45-49% is lifted code and ~22% is
  dispatch (`spu_lookup`, `spu_step`, `spu_indirect_branch`, `spu_drain_call`). Another ~6-7% is the interpreter
  (`spu_step`, `spu_interp_run_until_impl`), so some EDGE code still runs unlifted. Next: `SPU_INTERP_MISS=1` and
  a re-lift of what it finds.
- A static study of keeping GPRs in locals (lifter) found it feasible. No function is entered except at its start,
  and only `_u32[0]`/`_u16[0]` views are used. It needs a shadow-run harness (two lifts compared from one
  snapshot) before it can ship. It is parked behind the cheaper items.
- Re-lift from a `SPU_INTERP_MISS=1 SPU_LSA_GETLOG=...` run of the current build.
  - In that run the EDGE wids interpreted ~1.5 s per 20 s, mostly fragment placements not lifted yet (image 225 at
    LS `0x11730`, `0x112E0`, ...).
  - `lift_edge_frags.py`: 361 -> 438 placements.
  - `interp_miss_to_extra.py`: +40 job-module entries in `spu/jobmod_extra.json`, then `lift_jobmods.py`. 42 more
    targets lay past their module's image (module end = LS `0x4000` + its size), d935's at `0x11xxx` and 759d's
    above `0xEA66`. Those are not module code and were left out.
  - Swapped A/B against the build before the re-lift, no hang, the same 40 "unresolved" lines in both:
    - pair 2, both arms entering at 120 s: tail 2.212 vs 2.050 (+7.9%), EDGE ms/job 0.285 vs 0.302;
    - pair 1: 2.219 vs 1.848 (+20%), ms/job 0.267 vs 0.327. Its arms entered the level at 120 s and 210 s, so this
      pair overstates the gain.
  - A second `SPU_INTERP_MISS` run on the new build: EDGE interpreted time fell from ~650 to ~48 ms per 20 s per wid
    (-93%), and all workloads together from ~4 s to ~1.4 s per 20 s.
  - Known item, not chased: wid 7 (`Wws_Job/BatchJobList`, job module 759d) interprets ~1 s per 20 s. That code
    sits at LS `0x16670`-`0x359C0`, outside the module's image, staged somewhere the lift never saw (the WWS
    manager's second code buffer is a candidate; `SPU_JOBMOD_LOG=1` would show the code base). It is not on the
    frame's path.
- Profile of that build (runU, fixed sampler, 600-720 s):
  - On the three EDGE threads, ~50% is lifted code. Dispatch takes ~15-16%: `spu_indirect_branch` 5.5,
    `spu_drain_call` ~4.8, `spu_lookup` ~2.7, `spu_overlay_note_get` ~1, `spu_step` ~1, `spu_run_with_halt` ~0.9.
    MFC helpers take another ~5%.
  - Inside the hottest lifted functions (E550, 73E0, 4AB0, A880) the samples spread over 600-1400 16-byte buckets,
    with no hotspot. By instruction class:

    | class | share |
    |---|---|
    | vector/ALU | 48-57% |
    | `ctx` memory | 11-18% |
    | stack spills | 11-17% |
    | `pshufb` | 4-7% |
    | constants | ~5% |
    | LS memory | 1-3% |

  - Keeping GPRs in locals would mainly remove the `ctx` traffic and some spills, which is an upper bound of
    ~8-12% of an EDGE thread. Days of work and a new test harness, so it stays parked.
  - The dispatch path is the nearer target, but it is hang-sensitive (interrupt frames, setjmp in
    `spu_drain_call`).
- `scratchpad`-style helpers used for this (not in the repo): a 16-byte bucket classifier over the link map and
  the exe (capstone), and an asm counter per lifted function.
- Texture garbage is still there, but rarer. Over the 30 runs since the hang fix that have dumps at f_06600 and
  later, the 8-px chroma detector flagged 2:
  - `x15qs_1` (runQ): 5 frames, up to 0.058; noise on the golden wing meshes;
  - `im_2` (runU): 1 frame, 0.018; tiled columns in the background.

  Of the 18 runs that reach f_09000, that is 2, against 6 of 22 before. Both counts are small, so "rarer" is not
  established. The cause is still open.
- **Checked on a real PS3 (HEN, webMAN MOD PS3MAPI, read only; the console runs patch 1.12, our lift is 1.00).**
  - The game's `CellSpurs` sits at `0x3001AD00` (ours: `0x8001AD00`, the same offset in its region).
    `wklInfo1` is at `+0xB00`, `wklInfo2` at `+0x1000`, and there are 23 workloads.
  - The wid numbers differ from ours. The EDGE workloads are wids 19-21 there: their `arg` pointers are
    `0x14280` apart, exactly as our wids 15-17 are. Boomrang audio is wid 12 there (SPU 5 only).
  - 700 samples of `wklCurrentContention` (+0x20, high nibble for wids 16+), taken while walking in the first
    level: two or more EDGE wids running at once in **0** samples; exactly one in 349 (50%).
  - So EDGE is serial on the real console too, by the game's design. On hardware it occupies about half the wall
    time at full speed; here it fills 96% of a much longer frame. The lever stays the cost per EDGE job.
  - Also seen: SPURS on the console spreads EDGE over SPUs 0-5 (priority 3 on 0-3, priority 8 on 1-2 for wid 21).
- **Patch 1.12 vs our 1.00.**
  - The patched `GOWA.SELF` would not decrypt with our RPCS3 (`Failed to decrypt SELF metadata info`). Its loaded
    image was read from the running game through PS3MAPI instead. It stays outside the repository.
  - Code is +176 KB (+2.2%). The data segment moved from `0x800000` to `0x820000`, and `.bss` grew ~283 KB.
  - 53% of the 1.00 code's 64-byte blocks appear unchanged, at dozens of different shifts. The binary was relinked,
    so every address the port uses would move.
  - 12 of the 33 embedded SPU job modules changed, among them `d935`, the main EDGE module.
    `d935` itself barely changed (checked 2026-10-25, word by word over the module, same size and entry): 6 of its
    12576 words differ, 4 in the module header and 2 in one function where the two instructions that zero r28 and
    r32 swapped registers (same result). E550 and 73E0, the native stages, are byte-identical at the same
    addresses, so the native code carries over to 1.12 as is.
  - Moving to 1.12 means a full PPU re-lift, those 12 modules and the EDGE fragments re-lifted, and every hardcoded
    address remapped (best done with a function-matching tool). That is several days. Parked as a milestone for
    before any release, since most players will have 1.12 installed.
- **SPU MLAA off: no gain.**
  - RPCS3's public "Disable SPU MLAA - God of War: Ascension" patch (`il r2,0` at LS `0x4440`, `il r10,0` at
    `0x56C4`) matches our `gowtask0` (image 14): the original instructions there write exactly r2 and r10.
  - Both arms were re-lifted with the current lifter from a patched copy of the EBOOT and from the original one.
    A/B, no hang:
    - pair 1: tail 2.283 (MLAA off) vs 2.396 (on);
    - pair 2: 1.971 vs 1.950.

    EDGE ms/job is flat as well. MLAA runs outside the serial EDGE chain, so the frame does not shorten. The patch is
    not kept.
  - Side effect of the re-lift: the seven SPU images (`images.json`) are now lifted with the current lifter (a
    `_bt`/`_tg` rename, and `srepm0`'s `stop` now carries code `0x3FFF`). No hang in the four runs.
- **M0.62: the RSX walker had become a co-bound; a Vulkan backend fix frees it.**
  - A `PS3_FTRACE` of the current build (600-620 s, one instance) put the walker's drains at 470 ms of a ~526 ms
    mean frame, up from 344 ms at runP: the EDGE gains had moved the walker close to the bound. 3.0 s of the
    20 s were synchronous GPU waits from readbacks and texture creates.
  - `ENG_PHASE_STATS`: per 10 s the walker spent 3.7-4.1 s in the backend draw call (~65 us per draw), 1.9-3.6 s
    in textures and ~0.6 s in vertex fetch.
  - `VK_DRAW_PROF` (new) split `vk_eng_draw`: 41 us of it was the uniform copy. The engine wrote the shader
    constants straight into the shared UBO, and each batched draw read them back out into its ring slice. That UBO
    is host-coherent memory, write-combined and uncached on this driver (RTX 3080, NVIDIA 617.42), so reading
    ~8 KB took ~40 us. The constants now live in ordinary memory.
  - After the fix: descriptor work 41 -> 2.5 us per draw, backend 3.7-4.1 -> 1.0-1.3 s per 10 s, walker busy
    7-8.8 -> 3.9-5.4 s per 10 s.
  - Game frames/s barely move, because the frame is again bound by the serial EDGE chain. Swapped A/B, runY vs
    runX, no hang: tail 2.558 vs 2.527 (+1.2%) and 2.267 vs 2.206 (+2.8%). EDGE ms/job flat, EDGE ring waits
    (`ch8`) slightly lower. The walker now has slack for when the EDGE side gets faster.
  - Textures sampled through guest memory (`[eng-via-guest]`, new): the 1280x720 D24S8 zeta read as A8R8G8B8
    (fmt `0xA5`/`0xE5`), RGBA8 targets read as Y16_X16_FLOAT (`0xBF`), and sub-rectangles of the 1408x1408
    shadow map. These are the remaining readbacks if the walker becomes the bound again.
  - Upstream: the readback is in PR #532's batching path; the fix is pushed there.
- **M0.63: the WWS interrupt handler's generated stubs, +3.5%.**
  - A per-workload profile (`PS3_SAMPLE_WIDS=15,16,17`, new) shows where EDGE time goes:
    - lifted code 73%;
    - `spu_indirect_branch` 7.0%;
    - `spu_drain_call` 5.1%;
    - `spu_lookup` 1.6%;
    - MFC ~4%.

    The earlier "15% dispatch on the EDGE threads" was mixed with the audio workload, which makes ~4M of the ~4.4M
    indirect branches a second (`SPU_IB_STATS`, new).
  - EDGE's ~400k indirect branches a second are mostly the WWS policy module's stall-and-notify handler (LS
    `0xA2C`). On every interrupt it generates a stub at LS `0x3FEC0` that saves r0-r75, built from the template at
    `0xA40` minus `0x804` per quadword, plus a `bi $r79`. It calls the stub, runs the handler at `0x2390`, then builds
    and calls a restore stub (template `0xAD0`). That is ~13k interrupts a second per EDGE workload. The stubs only
    exist at runtime, so they ran in the SMC microstepper at ~25 ns an instruction.
  - The microstepper now runs straight sequences of `lqa`/`stqa`/`lqd`/`stqd` in a tight loop, with raw
    byte-swapped copies and no per-access probe. This only applies while every LS diagnostic is off.
    `spu_indirect_branch` went from 7.0% to 4.2% of EDGE samples.
  - Swapped A/B, runZb vs runY, no hang:
    - tail 2.688 vs 2.598 (+3.5%), EDGE ms/job 0.225 vs 0.232;
    - tail 2.712 vs 2.621 (+3.5%), EDGE ms/job 0.221 vs 0.230.
  - The handler itself (`0xA2C`) was no lifted entry of the policy module, so its stub-builder loop (`0xA60`)
    ran in the microstepper and then the interpreter (`stqx` is outside the microstep set). It is now an extra
    entry of `gowpm0` in `spu/images.json`. No EDGE workload is left in `SPU_INTERP_MISS`. A/B runZc vs runZb, no
    hang:
    - tail 2.663 vs 2.610 (+2.0%), EDGE ms/job 0.219 vs 0.223;
    - tail 2.792 vs 2.750 (+1.5%), EDGE ms/job 0.216 vs 0.220.
- **M0.64: `longjmp` without unwinding, +6-7%.**
  - Even with the A/B harness's per-channel timers off, 8.4% of the EDGE samples were in ntdll. `PS3_SAMPLE_MOD`
    (new) put 60% of those in `RtlVirtualUnwind2`, `RtlLookupFunctionEntry`, `ZwContinueEx` and `RtlUnwindEx`.
  - On 64-bit Windows, `longjmp` unwinds every host frame back to its `setjmp`, and the SPU runtime longjmps on every
    halt (each policy-module run's exit) and every iret.
  - `spu_jmp_no_unwind` zeroes the `jmp_buf`'s `Frame` right after `setjmp`, so the jump only restores registers. The
    jumps cross only C frames of the runtime and of lifted code. ntdll dropped to 2.8%, all condition-variable waits.
  - Swapped A/B, runZe vs runZc, no hang:
    - tail 2.935 vs 2.744 (+7.0%), EDGE ms/job 0.205 vs 0.220;
    - tail 2.802 vs 2.654 (+5.6%), EDGE ms/job 0.208 vs 0.222.
  - The A/B harness's `SPURS_PM_PROF` itself costs ~5% of EDGE time: two `QueryPerformanceCounter` calls per channel
    op. It is not set in normal play, so absolute tail numbers here are slightly pessimistic.
  - Level tail today: ~2.2 (runU, morning) -> ~2.8-2.9 game frames/s.
  - Bound re-checked on runZg (`PS3_FTRACE` 600-620 s, one instance): 74 frames, median 283 ms, against 38 frames
    on this morning's build. The main thread waits on EDGE for 168 ms per frame; the walker drains for 163 ms
    (~60%). EDGE ring waits (`ch8`) are ~1%, so the frame is still EDGE-bound.
  - EDGE-only profile now: lifted code 79%, runtime ~20% in pieces of 1-4% each (`spu_drain_call` 4.0%,
    `spu_indirect_branch` 3.7%, MFC ~6%, `spu_lookup` 1.5%), ntdll 0.2%.
  - 31-minute soak of runZg without harness instrumentation: no hang and steady memory. The later level runs at
    ~3.6-4.0 game frames/s (game frame 7951 -> 8611 between 1650 and 1830 s).
  - Inside the five hottest EDGE functions, context-memory accesses are 11-24% of samples and stack spills 11-16%.
    Register promotion in the lifter is the remaining double-digit lever, with a risk of trading context loads for
    spills.
- **M0.93: draw recording without repeated state, descriptor sets reused (2026-10-30).**
  - Where the walker's backend time goes now (x190, `VK_DRAW_PROF`, ~2500 draws a frame, 96% into an open render
    pass): descriptor set 1.5 us per draw (allocation 0.3, uniform copies 0.3, view/sampler resolve 0.2, update
    0.5), vertex/index copies 0.6, command recording 1.9, batch flush 1.0-1.1.
  - Toolkit `VK_STATE_SKIP` (port default on): the pipeline, viewport, scissor, stencil reference and depth
    bounds are set only when they differ from what the command buffer already holds. A/B, one exe (x191, k o o k):
    recording 1.9-2.0 -> 1.4-1.5 us per draw; 12.31 / 12.45 against 12.35 / 12.29 game frames/s (within noise).
    Exact, so it stays on.
  - Toolkit `VK_UB_DYNAMIC` (port default on): the uniform bindings take dynamic offsets, so a draw with the
    previous draw's views and samplers binds its descriptor set again. A/B, one exe (x192, u o o u, same spot):
    descriptor work 1.5 -> 0.9 us per draw, backend 5.0 -> 4.5; 12.53 / 12.29 against 12.19 / 12.41 game
    frames/s (within noise). Frame dumps look the same.
- **M0.92: the MLAA input copies stay on the GPU (2026-10-30).**
  - With the new defaults the walker's largest remaining GPU wait was the readback before NV3089 blits (x182
    trace: ~1.05 s per 20 s). Logged blits (x183, `NV3089_GATE`, 5 s): every one is a 1:1 copy of the display
    buffer (three, in local memory) into main memory at offset `0xAB0000`, pitch 5120, in two strips (1024 and
    256 columns), ~2 a frame: the input of the SPU MLAA. Its taskset is served by HLE (M0.17), which never reads
    those pixels, and the frame then samples that memory as one 1280x720 linear A8R8G8B8 texture.
  - Toolkit `RSX_BLIT_SHADOW` (d1571122): such a blit is copied on the GPU into a strip texture keyed by its
    destination (backend op `image_copy`, batched); a texture fully covered by strips gets them assembled on the
    GPU. Guest memory is not written. Port default on only while MLAA is HLE (`GOW_MLAA=lifted` leaves it off).
  - Check mode (`=2`, x185): ~4750 assembled textures, 0 texels differ from the guest-memory copy.
  - A/B, one exe (x186, 360-590 s, quiet host): texture re-uploads ~630 -> ~300 per 30 s; 12.32 on against
    11.95 / 11.94 off in the same spot (+3%); the second on run stood elsewhere (12.80).
  - Tried, not adopted: `VK_DRAW_BATCH_MAX=512` (x187, one exe, m d d m): backend recording 3.9-4.1 against
    5.0-5.6 us per draw, but the texture phase rose 4.6-4.9 -> 7.0-7.8; 12.05 / 11.86 against 12.40 / 12.02.
    The default (128 with `VK_ASYNC`) stays.
  - The bound after M0.92 (x188, runVr, timeline 420-440 s under PS3_FTRACE, so 11.5 rather than ~12.4 at speed;
    thread CPU 400-425 s): the main thread waits ~4 ms a frame for the flip (21-31 ms before M0.89), the walker
    drains 79 of an 87 ms frame with ~2 ms of GPU waits, five SPU kernel threads are ~90% busy. Thread CPU:
    main 0.92, walker 0.84, SPU threads 0.81-0.83, sum ~6.3 cores on the 6-core host.
  - Causal probe (x189, runVr, quiet host): `RSX_SLOW_PCT=25` (the walker busy-waits 25% of each pass) gives
    10.55 against 12.48 game frames/s, -15.5% where a fully binding walker would give -20% (M0.80: -13%). The
    walker is still the main bound; 12.48 is the production rate at the door at speed.
  - The old "SPU MLAA off: no gain" (M0.6x) was measured at ~2.3 fps with a patch that disables the SPU tasks,
    not these GCM copies; it says nothing about this path.
- **M0.91: colour snapshots join the draw batch, +4-7% in the corridor (2026-10-30).**
  - A draw that samples a target it is drawing into gets a copy of it (`color_snapshot`, ~11 a second in the lit
    passes). Each copy was submitted on its own with a wait for the GPU (~2.7 ms). Toolkit `VK_SNAP_BATCH`
    (5d905815) records it into the open batch after a full barrier; port default on.
  - A/B, one exe (x180/x181, 360-590 s): walker snapshot time ~0.95 s -> ~6 ms per 30 s; walker 24.9 against
    27.1 us per draw and 11.33 against 10.61 game frames/s in the same-scene pair (+6.8%, a terminal at ~0.9
    cores in both runs); 9.86 against 9.48 in the second pair (+4%, the two runs stood in different places).
    Frame dumps look the same.
- **M0.90: colour targets read in another format converted on the GPU (2026-10-29).**
  - Two half-resolution A8R8G8B8 targets (640x360 at `0x024A0000`, 320x180 at `0x029F0000`) are sampled as linear
    Y16_X16_FLOAT (~2-3 reads a frame). Each read went through guest memory: a readback that waits for the GPU
    (~2.9 ms), a CPU convert and a re-upload. Toolkit `RSX_C2RG16` (bc1d7ea9) regroups the bytes on the GPU (image
    -> buffer -> compute pass -> R16G16F texture) inside the open batch; each target keeps two converted shapes.
  - Check mode (`RSX_C2RG16=2`, x177/x178): ~20000 conversions, 0 texels differ from the guest-memory path.
  - A/B, one exe (x179, 360-590 s): the walker's store time 1.45 s -> 0.02 s per 30 s, texture phase 7.2 against
    8.5 us per draw; 9.66 / 9.46 on against 9.51 off (the fourth run void). A terminal used ~0.9 cores through all
    four runs, so the absolute rates are low. Port default on (`RSX_C2RG16=1`, main.cpp).
  - RPCS3's RSX code, read for ideas only (GPL-2.0; nothing copied): per-draw work is skipped unless the
    registers it depends on changed (dirty groups), textures are cached per unit with guest page write
    protection instead of hashing, vertices go to the GPU raw and the vertex shader decodes them, descriptor
    sets and constants are reused. Those four are the next walker steps, in that order.
- **M0.89: the RSX walker binds the frame again; range fetch and render-pass merging on by default (2026-10-29).**
  - Per-thread host CPU at speed (x168, `Get-Process` thread times over 400-430 s, quiet host): main thread 0.97
    cores (about half of its sampled guest time in the job-list polls `416338`, `466E38`, `416610`), RSX walker
    0.89, SPU kernel threads ~0.6 each. Frame timeline (x166, 420-450 s, one other program at ~0.7 cores): the walker
    drains the FIFO for the whole frame (117 ms per 111 ms frame, 11.8 ms of it waiting on synchronous GPU
    submits), and the main thread waits 29 ms per frame at `0x375A88` for the flip counter.
  - The kernel threads nap ~33% of the time between empty passes (`Sleep(1)`, median 1.7 ms). Toolkit
    `SPURS_PASS_SLEEP_US` (abfaa4bc) waits on a high-resolution timer: with 200 the nap is ~0.53 ms but the threads
    still nap ~28% (they have no work). A/B, one exe (x170, 360-590 s): 11.55 / 11.46 against 11.44 (the fourth
    run had a terminal using ~0.7 cores): no change. Default off. `SPURS_PASS_SLEEP=0` (x169) makes every kernel
    thread spin at ~0.94 cores; that run had ~1.6 cores of other load and is not counted.
  - Walker profile (x168, 50 ms sampler): `rsx_vertex_remap_build` 19.6%, `rsx_vertex_fetch_many` 12%, FIFO decode
    8%, texture content hash 5.5%. `ENG_PHASE_STATS`: ~2400 draws per frame, ~28 us each (backend recording ~8.5,
    vertex fetch ~7.5, textures ~7, pipeline lookup ~3.5).
  - Toolkit `RSX_REMAP_RANGE` (4e04a0a9, default off): a dense indexed draw takes its id range instead of
    deduplicating references. A/B, one exe (x172/x173, 360-590 s): clean pair 11.97 on against 11.21 / 10.91 off,
    vertex fetch 6.0 against 7.0-7.2 us per draw. The other three runs had a terminal using ~0.85 cores (and two
    of them stood elsewhere in the scene): 9.40 on, 9.19 off / 7.85 on. Not adopted until a clean pair confirms
    it. Its frame dumps look the same as the off runs (visual check, not pixel-compared).
  - Second pair (x174): 10.07 on against 10.31 off, but the on run's walker cost 38.4 against 31.1 us per draw
    (texture phase doubled: another part of the scene) with a browser at ~1 core; not comparable. Vertex fetch
    fell in every pair (7.0-7.6 -> 6.0-7.1 us per draw).
  - Toolkit `VK_PASS_MERGE=1` (M0.81, off until now) on top, A/B one exe (x175, range on in both arms):
    backend recording 4.8 against 8.4-8.6 us per draw, the walker's whole method time 26.9 against 29.1-29.4 us
    per draw (-8%). Same-scene pair (both at the door): 11.18 on against 11.04 off. The other two runs stood by
    the statue; the merge one is void (terminal ~0.9 cores, texture phase doubled).
  - Both are port defaults now (`VK_PASS_MERGE=1`, `RSX_REMAP_RANGE=1`, main.cpp; 0 goes back): they take ~10%
    of the walker's time per draw and the frame dumps look the same, but the frame rate moves only +1-3%. The
    walker and the main thread bind together: in x171 the main thread is busy ~55 ms of an 84 ms frame, so a
    free walker would cap near ~16 game frames/s before the main thread's job-list polls bind.
  - 20-minute soak with the new defaults (x176, environment unset): no hang, steady memory (2.2 GB private),
    frame dumps right from the menus to the door. Its rate (7.8) is not counted: a terminal used 0.9-1.8 cores
    for the whole run.
  - Synchronous GPU waits on the walker (x171 trace, 20 s): `vk_eng_color_snapshot` 0.61 s, `vk_eng_readback`
    0.54 s, `vk_eng_texture_create` 0.40 s, ~7.5% of its time. Next walker targets with the texture phase.
  - Measuring: winsheet first; `methods` us per draw (ENG_PHASE_STATS) more than 10% apart voids a pair.
- **M0.88: General6's Perlin noise as native code: the job is 47% faster, the frame rate does not move (2026-10-29).**
  - The hot block of job module a67e (`4930`, falling into `4AB0`'s entry block) is Ken Perlin's improved noise in
    3D, four points per call (point i in words 0-2 of r(3+i), result in word i of r3). The callers sum four octaves.
    The tables are read from local store at run time (permutation at `0x8680`, gradients at `0x8580`).
    `port/gow_a67e_noise.c` computes the four points side by side with the same `spu_helpers.h` float ops in the
    same order, so every lane is bit-exact. `port/gen_a67e_noise_lift.py` puts a switch in front of the lifted
    function (`GOW_A67_NOISE`: unset/1 native, 0 lifted, 3 check).
  - Job lab (300 captured a67e jobs): native 300/300 identical in final registers, LS and outputs, median 0.92 ms
    per job vs 1.74 ms lifted (-47%). Check mode in the lab: 300/300 jobs, no difference printed.
  - In game (x165, `SPURS_PM_PROF`): General6's kernel-thread time fell from 77% to 52% of the window for the same
    job count, and the host sampler on it now shows the noise at ~23% of its time (was ~45%).
  - Corridor A/B (x164, 600 s runs, 360-590 s, order native/old/old/native): 10.36 vs 10.35 game frames/s in the
    first pair. The second pair (8.98 old, 6.29 native) is not counted: other programs used one to two cores
    through both of its runs (video encoding, an archive, a GPU stress test, a browser). The frame rate is
    neutral: General6's compute is no longer what the frame waits for. The native path stays the default since it
    is exact and frees a quarter of a host core.
  - The main thread's sampled waits (`GOW_MAIN_PROF`) are unchanged by it: ~20% of its time at `0x375BF0`
    (lr `0x375A88`), ~13% at `0x467B2C`, ~12% at `0x416A4C`, in both the old (x160) and the new (x165) build.
- **M0.87: SPU floats with flush-to-zero, quadword local-store access in one load, +4% in the corridor (2026-10-28).**
  - Toolkit `SPU_FP_DAZ` (on whenever `SPU_FP_NATIVE` is): the SPU thread's MXCSR also sets FTZ and DAZ. SPU single
    precision has no denormals (exponent 0 reads as zero, a result below the normal range is +0), so the native
    `fa`/`fs`/`fm`/`fma` no longer check their inputs for denormals; the exact path still covers exponent 255 and
    overflow. Double-precision helpers clear FTZ/DAZ with the rounding mode. `test_spu_fp_daz` (new): 480M lane results
    of six ops against the exact path, 0 differ; without DAZ in MXCSR the same test reports 17.9M differences.
  - Toolkit `SPU_LS_VEC` (SSSE3 x86 hosts): `spu_ls_read128`/`spu_ls_write128` are one 16-byte load or store and one
    `pshufb` word swap. The old form stored four byte-swapped words and reloaded them as a quadword, which stalls on
    store forwarding.
  - a67e replay (job lab, 300 jobs, 300/300 identical): ~2.17 -> 2.12 ms per job with `SPU_LS_VEC`, then ~1.50 ms
    with `SPU_FP_DAZ` (-29%).
  - Corridor A/B (x159, 360-690 s, order old new new old, host quiet): previous build 11.14 / 10.90, new build
    11.35 / 11.65 game frames/s (+4.4% on the means). Scenes match. The new build is the reference.
- **M0.86: BatchJobList's position-independent code lifted (relocatable lifts); the wait moves to General6 (2026-10-28).**
  - Toolkit: `spu_lifter.py --reloc` lifts position-independent code once at a canonical base; every address the
    code derives from its own pc (links, `lqr`/`stqr`, transfer and return pcs) adds `ctx->reloc_delta`.
    `spu_overlay_register_region_reloc()` makes a copy resident wherever the title DMAs it, and dispatch sets the
    delta. The four blobs are pc-getter PIC (`brsl $rX, .+4`; no absolute branches or `lqa`/`stqa`).
    `analysis/lift_reloc_blobs.py` lifts them (canonical base `0x20000`, images 900-903); `SPU_INTERP_MISS` now reports
    misses inside such code at the canonical pc, ready to pass back as entries.
  - Effect on the SPU side (x136): interpreted SPU time 13.6 -> 4.4 s per 20 s, BatchJobList 77% -> 29% busy, all the
    workloads 497% -> 449%. No hang and clean frame dumps in every run.
  - Effect on the frame (x144, one diagnostic exe, poll sites as spans, 420-480 s): with the lifts the main thread's
    wait on BatchJobList (`0x464F84`) fell from 9.55 to 4.00 s per 60 s, but its waits on General6 grew
    (`0x375A88` 6.68 -> 9.25 s); 10.81 vs 10.46 game frames/s. Corridor A/B (x142, quiet host, new vs previous
    build): 11.04 / 10.63 vs 11.22 / 11.64. The frame rate is neutral within noise, so the port keeps them off
    (`SPU_RELOC_OFF=1` default; `=0` runs them). General6 (job module a67e, `4AB0`) is the next bound.
  - Still interpreted in BatchJobList: targets outside the four blobs (`0x359C0` ~1.5 s per 20 s, `0x18BD0`,
    `0x20BD0`), code from other sources; not chased while General6 is the bound.
  - Timing notes from the same day are in DEBUGGING.md ("Before trusting a timing"): a full drive and a busy host
    each cost 25% or more, which an A/B between builds cannot tell from a regression.
  - The toolkit change went upstream as PR #636 (ported to v0.18 with a unit test; CI green).
  - New benchmark recipe (DEBUGGING.md: attacks stop at 300 s): the production build gives 11.70 / 11.72 game frames/s.
  - General6 on more lanes does not help (x149 vs x150, one diagnostic exe, new recipe, 420-480 s): with
    `3=6` General6 really runs on 4 SPUs 24% and on 5-6 SPUs 29% of its active time, yet the main thread's wait on
    it (`0x375A88`) grows 10.22 -> 11.48 s per 60 s and the frame rate falls 11.77 -> 9.53. Its job list has a serial
    chain: only faster jobs shorten it.
  - Where General6's time goes (x148, host sampler on wid 3 only): job module a67e `4AB0` 46%, a67e `63B4` 12.7%,
    job module 1bdd ~30% (`59C0` 8.1%, `53E0` 5.6%, `4450` 5.1%, `A588` 5.0%, `73E0` 2.7%, `40B8` 2.6%), a67e `4930`
    2.4%. `4AB0` is a ~2200-line lifted function of float math (231 `fma`, 178 `fm`, 176 `fa`) and shuffles (460
    `shufb`): the next native-stage candidate, by the EDGE_NATIVE.md recipe. The shuffle and rotate helpers already
    use `pshufb`.
  - Job lab on General6 (lab exe, `SPU_JOB_CAPTURE_WIDS=3 SPU_JOB_CAPTURE_IMG=218`): 300 a67e jobs captured, replay
    300/300 identical, ~2.2 ms per job. A block count of `4AB0` shows that only its entry block runs: 780 straight-line
    operations (113 indexed `lqx` gathers, 190 `shufb`, 80 `fa`, 44 `fm`), called 1920 times per job. Register
    promotion of that block gave no gain in replay.
  - Toolkit: `shufb` and the quadword byte rotates/shifts no longer swap bytes around `pshufb` (the index is remapped
    instead, SSE4.1 path). Exact: `test_spu_shufb` passes 200000 random cases. a67e replay 2.227 -> ~2.17 ms per job
    (-2.6%). Corridor A/B (x156, new recipe, 360-690 s): previous build 11.58, new build 11.62 game frames/s (the second
    pair is not counted: one run had a browser using about a core, the other ran during bursts of host system load). Neutral to slightly positive; the new build is the reference.
  - The walker (`ENG_PHASE_STATS`) now spends ~77% of its time in methods (was 67%).
- **M0.85: the same two lists as kernel-thread lanes, ~10.8 game frames/s in the corridor (2026-10-28).**
  - `SPURS_LANES` (the mechanism the EDGE lists already used) lets up to n of the six kernel threads run a workload
    at once, each as the SPU it plays (its own spuNum), with the rerun window kept and no thread created per
    dispatch. General6 and BatchJobList have a priority on SPUs 0-5, so every kernel thread may take them.
  - Port default now `SPURS_LANES=15,16,17=3;3,7=3`, with no `SPURS_MAXCONT` default. Corridor, 360-690 s:
    - lanes vs `SPURS_MAXCONT=3=3,7=3` (x131, M L L M): 10.82 / 10.91 vs 9.03 / 8.60 game frames/s;
    - adding `9,10,11,13` to the lane group (x132, L P P L): 10.82 / 10.64 vs 10.63 / 10.77, noise, not adopted.
  - From ~7.4 before M0.84 that is about +45%. No `[pm-hang]` and clean frame dumps in all eight runs.
  - 25-minute soak with the new default (x133, environment unset): no hang, no `[pm-hang]`, longest gap between
    flips after the boot 4.8 s at ~280 s. From ~250 s on, this run's bot fought in another part of the area (by
    the stairs, more enemies on screen) than the door the A/B runs stood at, so its rates (9.56 at 360-690 s, 10.01
    at 690-1100 s, 10.28 to the end) are of a different scene. The pad script is timed in wall-clock, so where the
    bot ends up can differ between runs: compare A/B arms only while their frame dumps show the same place. All
    eight x131/x132 runs were at the door at both 360 s and 690 s (the dumps nearest those times, picked by file
    time).
  - Frame timeline after the change (x134, poll sites as spans again, 420-480 s at the door, 10.65 game frames/s):
    the main thread is blocked ~66 ms of a ~95 ms frame (41.4 s in 630 frames): `0x416A44` 10.7 s, `0x464F84`
    9.1 s, `0x3E8910` 7.3 s, `0x416654` 7.3 s and a new one, `0x375A88`, 4.9 s (General6 busy on 2.3 SPUs meanwhile).
    During every one of those waits 5.0-5.7 of the 6 SPU threads are busy, and `SPURS_PM_PROF` (x135) puts the
    workloads at 497% of 600% with 2% idle: the SPU pool is now saturated. (M0.86 below: cutting BatchJobList's SPU
    time shortened its wait, but the frame then waited on General6 instead, so the frame rate barely moved.)
  - Where that time goes (x135, `SPU_INTERP_MISS`): General6 95% busy and 0% interpreted (it is lifted; most of it is
    job module a67e's `4AB0`), the three EDGE lists 62% each and 0% interpreted, the audio workload 66% on its own
    SPU, and BatchJobList 77% busy and **87% interpreted**: ~13 s of every 20 s of interpreted SPU time, nearly all
    the interpreter time left. Its code comes from four EBOOT blobs (`0x716730` 0x2450 bytes, `0x6D66B0` 0xB80,
    `0x6AC8B0` 0x28E0, `0x6AF1B0` 0x32E0) that its jobs copy to a new 16-byte-aligned LS address at each load (x112:
    299 loads, 299 different addresses), so a lift per placement cannot cover it; a position-independent lift can.
- **M0.84: General6 and BatchJobList run on three SPUs each, +19% in the corridor (2026-10-28).**
  - The main thread's longest waits in the corridor frame (x125: poll sites traced as `PS3_FTRACE` spans with a
    temporary lift patch) are `0x464F84` (41 ms of a 125 ms frame, while wid 7 BatchJobList runs) and `0x3E8910`
    (33 ms, General6), then `0x416A44` (15 ms) and `0x416654` (12 ms). During the `0x3E8910` wait only 2.15 SPUs are
    busy, and every non-EDGE workload runs on exactly one SPU at a time: the title registers maxContention 1 for all
    of them.
  - `SPURS_MAXCONT=3=3,7=3` (port default until M0.85 replaced it with `SPURS_LANES`) runs those two lists on up to three virtual SPUs, each with its own
    spuNum (lanes 1-2 on worker threads). Corridor, 360-690 s:
    - `3=3` alone vs off (x126, x127): 8.01 / 8.21 / 8.12 vs 7.45 / 7.28 / 7.50 game frames/s;
    - `3=3,7=3` vs `3=3` (x128, x129): 8.70 / 9.07 / 8.93 / 9.16 vs 8.31 / 8.18;
    - `3=6` (clamped to the instance's SPUs) is no better than `3=3` (8.55 / 8.07);
    - adding `9,10,11,13=3` is much slower (5.75 / 6.04): a lane run creates its worker threads at every dispatch
      and skips the rerun window, which costs more than it gives on short, frequent lists.
  - No hang in any of the 14 runs; the frame dumps show the corridor and combat drawn as before. The lanes claim
    jobs through the job list's GETLLAR / PUTLLC atomics; a lost update there would leave a count above zero and
    hang the main thread's poll, which no run showed.
  - 25-minute soak with the new default (x130, environment unset): 8.90 game frames/s at 360-690 s, 8.67 at
    690-1100 s, 8.96 to the end; no gap between flips beyond the ~140 s load; frames clean.
  - Next on the same path: persistent lane workers (no thread per dispatch, rerun kept), which may also make the
    shorter lists worth a lane.
- **M0.83: the lifted PPU code builds for x86-64-v3, +2.5%; the corridor frame is main-thread bound (2026-10-28).**
  - Frame timeline of the corridor (x117, `PS3_FTRACE` 420-480 s, re-lifted exe): median frame 123.9 ms. SPUs 0-3
    are busy ~60% of it, SPU 4 87% (wid 8, the audio workload, alone on it; not on the frame's path), the walker
    drains for 85.7 ms. Inside each frame there is a ~40 ms stretch where SPUs 0-3 are idle and the walker only
    drains in short bursts (`D D D D`): only the main thread works then, so it is the frame's critical path.
  - The main thread's 22.7% at `0x3E8900` is a poll of a u16 at `[[r30+0xC]+0xE0]` (loop `0x3E8910`) after the
    frame routine's calls; `0x416A44` and `0x464FA4` are other polls (main-prof ~8% and ~18%). Which writer clears
    them is not known yet.
  - The `0x3E8900` poll, followed through (x119-x123): `[0x9397F8]` = `0x80FF8CA0`, the same in three dumps, so
    the polled u16 is at `0x80FF8D80`. A 1 ms byte-change watch (`GOW_LLE_WATCH=1 GOW_LLE_DUMP_EA=80FF8D00`) shows it
    set to N and counted down to 0 (~110 times a second): a job list's remaining-job count. `SPU_ATOM_EA=80FF8D80`
    names the writer: PUTLLCs from the WWS policy module (image 10, pc `0x2628`). `SPU_ATOM_WIDS=60` puts every
    atomic on that line in wid 3 (General6). So the main thread waits for General6 to finish its list, ~28 ms a
    frame. General6 spends 61% of its time in job module `a67e`'s `4AB0` and 15% in `63B4` (earlier profile).
    `GOW_GUARD=80FF8D80` reported no write at all on that page, though the PPU and the SPUs both write it.
  - `PPU_HOST_ARCH` (port CMake, default `-march=x86-64-v3` like `SPU_HOST_ARCH`, with `-ffp-contract=off` since FMA
    then exists): corridor A/B in exes g f f g f g (x118), 7.45 / 7.63 / 7.55 against 7.44 / 7.25 / 7.39 game
    frames/s, +2.5% mean, the new build ahead in all three pairs. `-DPPU_HOST_ARCH=` brings back the SSE2 baseline.
- **M0.82: real gameplay runs at about half the benchmark rate; the corridor's EDGE fragments lifted (2026-10-27).**
  - The benchmark scene (360-720 s) was the Megaera dodge tutorial: the pad script only pressed X, so Kratos kept
    getting hit and the game never moved on. With the right stick flicked every 400 ms (`PAD_STICK` 7th field,
    toolkit 2f614c87) the dodge passes, and pressing square / triangle / circle about three times a second through
    `PAD_FILE` (`scripts/pad_masher.sh`) passes the attack tutorial and the prison. The game then reaches a corridor
    in front of a door at ~270 s and stays there.
  - In that corridor the game ran at ~6.3 game frames/s (~10 presents/s), against ~12.7 in the tutorial loop. The
    README's number was the tutorial's. The corridor (360-690 s) is now the benchmark scene.
  - What differs there (x110, x111): the RSX walker was ~67% busy (20 s of methods in 30 s; ~17.6k draws/s), and a
    third of the sampler's hits were in the SPU interpreter. `SPU_INTERP_MISS` put 37.5 s of interpreted SPU time in
    every 20 s: the three EDGE workloads were ~82% busy each with 62% of it interpreted (EDGE code fragments at
    placements no earlier run had captured), and wid 7 (job module 759d) was 48% busy, 88% interpreted.
  - A `SPU_LSA_GETLOG` capture in the corridor (x112) gave 131 new EDGE placements; `lift_edge_frags.py` now has
    569. The script now passes the function entries of an existing lift again (a re-lift from new logs dropped the
    entries that came from older logs) and keeps the lift the port patches at build time (`edgefrag_7228c0_11150`)
    as it is.
  - After the re-lift (x114): EDGE workloads ~42% busy each with 0% interpreted, all SPU kernels 4.57 -> 3.54
    cores, interpreted time 37.5 -> 6.8 s per 20 s (what is left is wid 7). Frame rate, corridor A/B e d d e
    (x113): 7.44 / 7.08 against 6.93 / 7.03 game frames/s, +7.4% and +0.7%. A core of SPU time went away and the
    frame rate barely moved, so the SPUs are not what limits the corridor.
  - wid 7's code is staged by the job itself: EBOOT blobs (`0x716730`, `0x6D66B0`, `0x6AF1B0`, `0x6AC8B0`, up to
    `0x32E0` bytes) copied to LS addresses that change from job to job (299 placements in one run), so lifting one
    copy per placement does not scale. It is ~0.33 cores interpreted.
  - Upstream PR #619's lost-wakeup fix (toolkit b606ff7c, `SPU_WAKE_LOCK=0` skips it): corridor A/B w n n w (x116)
    7.09 / 7.11 against 6.75 / 7.21, noise. Kept on as a correctness fix.
  - The corridor's limit (x115, re-lifted exe, 6.9 game frames/s): the main thread at ~0.96 cores with 22.7% of its
    samples in the frame routine's wait at `0x3E8900` (22.0% in x110, before the re-lift), the walker ~68% busy, the
    SPU kernel threads ~0.55 cores each. Nothing is saturated, so the next step is a frame timeline (`PS3_FTRACE`) of
    the corridor: who waits for whom within a frame.
- **M0.81: the walker's pipeline lookup without the linear scan, +4% (2026-10-27).**
  - `eng_pipeline_get` runs on every draw (~2000 a frame). It compared the key with every cached pipeline in turn,
    and walked the fragment program four times: sampled units, gamma units, sign-expansion units and the
    structural hash.
  - A key-hash index over the pipeline list finds the same entry the scan did (a key is added only when it is not
    found, so keys are unique). A 16-entry memo, validated by a copy of the program's bytes like the existing
    vertex-program memo, keeps the three unit sets and the structural hash per program; the per-draw texture
    masks are applied to the stored sets. Inline constants are still collected every draw (toolkit ae236f41).
    `RSX_PIPE_FAST=0` turns both off.
  - A/B, one exe, env alternated f n n f (x104), game frames/s over 360-720 s: 11.81 / 13.27 against 11.55 / 12.53,
    +2.3% and +5.9%, no hang, nothing else above 0.2 cores. The spread between the two pairs (~1.3 frames/s) is
    larger than the effect, so the size is uncertain; the lookup returns exactly what the scan returned, so it is
    kept.
  - Where the walker's driver time goes (x105, `PS3_SAMPLE_MOD=nvoglv64.dll`): the NVIDIA Vulkan driver is at most
    ~24% of the walker's samples, and 79% of the driver samples are entered from `vk_eng_draw`. Each draw began and
    ended its own render pass behind a full memory barrier and an image barrier per target; `VK_DRAW_PROF` put
    recording at ~5.7 us of the draw.
  - Tried, not adopted as a default: `VK_PASS_MERGE=1` (toolkit e5a4814c) keeps a batched draw's render pass open
    for the next draw with the same render pass, framebuffer and size that samples none of the pass's targets.
    96% of draws went into an open pass, no draw sampled a target of its own pass, recording fell to 2.2-2.4 us,
    and the frame dumps looked right (x106). A/B f n n f (x107): 12.75 / 12.73 against 12.64 / 12.40, +1.8%,
    under the 0.44 noise floor, with a browser using ~0.5-0.7 cores in three of the runs. The walker's own CPU
    fell from ~0.86 to ~0.78 cores without the frame rate following, so after M0.80 and M0.81 the walker is no
    longer the only limit. The knob stays, off.
- **M0.80: EdgeWatchdog sleeps instead of spinning, +5.8%; the RSX walker is the binding stage (2026-10-27).**
  - Per-thread host CPU over 120 s (x101, `Get-Process` thread times, tids matched to the all-thread sampler): main
    thread 0.91 cores, RSX FIFO walker 0.87, the busiest SPU kernel thread 0.85, the others 0.73-0.77, and one
    above-normal-priority thread at 0.77 cores that the sampler did not cover. Guest threads with priority below 256
    get `THREAD_PRIORITY_ABOVE_NORMAL`.
  - That thread is EdgeWatchdog. It polls with ~800 us `sys_timer_usleep` calls, and the runtime serves a sleep
    under 1 ms with a `QueryPerformanceCounter` loop around `SwitchToThread`, so the watchdog held a logical
    processor for the whole sleep. `PS3_USLEEP_WAIT_US=<n>` (toolkit fd12038a) waits a sub-millisecond sleep of
    at least n us on a high-resolution waitable timer instead; shorter sleeps, like the main thread's 30 us polls,
    keep the precise spin. The port sets 500.
  - The walker: in the x99 traces its drain passes cover 57.8 of 60 s with no ACQUIRE block, and the flip reaches
    it ~23 ms after the guest requests it. `ENG_PHASE_STATS=30` (x102) shows a constant ~22.9 s of every 30 s in
    methods, 20.3 s of it in 580-700 thousand draws (~2000 per frame, ~30 us each): backend recording 6.2 s,
    textures 5-6 s (hash 1.4, re-upload 1.5-1.8, surface stores 0.9-1.7), vertex fetch 4.3 s, pipeline lookup
    3.5 s, index build 0.7 s.
  - Causal check: `RSX_SLOW_PCT=<n>` (toolkit 5f4740df, diagnostic) busy-waits n% of each walker pass. At 25 the
    game ran 10.65 game frames/s against 12.21 / 12.39, -13% where a fully binding stage would give -20%.
  - A/B, one exe, env alternated b s w b w (x103), game frames/s over 360-720 s: 13.02 / 13.02 with the watchdog
    timer against 12.21 / 12.39, no hang; the game used ~6.6 cores against ~7.5, and nothing else used more than
    0.5 cores during the runs.
  - Next: per-draw work on the walker (pipeline key, vertex fetch, texture checks, backend recording).
- **M0.79: indirect-branch dispatch cache, dispatch share 14.1% -> 11.6%, frames/s within noise (2026-10-27).**
  - Most indirect branches on the render/job chain go to a few targets: in an x84 census of ~700k branches/s, one
    target was 12% and the top 24 were 44%. Each one still walked every check in `spu_indirect_branch`, up to
    16 resident code slots, and up to three `spu_lookup` probes into a 96 KB per-thread table that the lifted code
    between dispatches evicts.
  - A per-thread cache of 512 16-byte entries, keyed on pc, image, overlay and policy mode, now answers the common
    case (toolkit 4f4c1418, 7934d504). An entry is written only when the standard lookups found the target and
    nothing else applied; job entry and the resident code slots are checked live on a hit, and the paths that read
    local store are never cached. Any function, task-entry or task-ELF registration bumps one epoch that empties
    the cache. `PS3_SPU_IBC=0` turns it off, as does any trace switch the dispatcher reads.
  - A bitmask of the resident code slots in use lets dispatch skip the 16-slot loop when no slot is in use. The
    `spu_lookup` entries shrank from 24 to 16 bytes, so the table is 64 KB and no entry straddles two cache lines.
  - The cache alone did not help. In a host profile (x94) the cost moved from `spu_lookup` into
    `spu_indirect_branch`, and the dispatch group stayed at 14.3% (14.1% before): the slot loop and three global
    loads for the generation still touched cold cache lines. With the slot mask and the single epoch (x95) the group
    is 11.6% (`spu_indirect_branch` 6.4%, `spu_lookup` 1.1%, `spu_drain_call` 4.1%), and `spu_overlay_note_get`
    fell from ~1.3% to 0.8%. On this host, cost per dispatch follows the cache lines it touches, not the
    instructions it runs.
  - A/B without `SPURS_PM_PROF` (x96, alternating, one instance at a time), game frames/s over 360-720 s: 12.22 /
    12.11 against 12.17 / 11.92 for M0.78, +0.4% / +1.6%, inside the noise floor. No hang. Nothing else used more
    than 10 CPU-seconds per 30 s during the runs.
  - The per-frame ftrace numbers look larger than that and are not a result: SPU busy 360.3 / 357.4 against
    397.1 / 380.6 ms per frame and drain 75.4 / 76.8 against 85.7 / 82.4, but the 20 s trace window held 250 / 251
    flips against 222 / 233, and the total SPU busy time in it is the same in both builds (90.1 / 89.7 against
    88.2 / 88.7 s). The heartbeat for the same stretch does not show the extra flips. A per-frame
    number from one 20 s window moves with the flip count, so compare window totals when the counts differ.
  - The build is kept for the lower dispatch cost (x95) and because it is not slower; it does not move frames/s.
  - Tried after it, not adopted: pinning each SPURS kernel thread to its own physical core. The game uses ~6.5 of the
    host's 12 logical processors (6 cores), so kernel threads share cores with SMT siblings. `SPURS_PIN=1` (toolkit
    branch gow-v017pin, off by default) gives kernel thread k the first logical processor of core k. One exe, env
    alternated (x97): 11.43 / 11.50 game frames/s pinned against 11.91 / 12.04 unpinned, -4% in both pairs, no
    hang. A hard pin keeps a kernel thread waiting when the main, RSX or Vulkan thread holds its processor, where
    the scheduler would have moved it.
  - Also tried, not adopted: letting the main thread's wait loops yield. A host sample of every thread with the main
    thread's guest pc (x98, `GOW_MAIN_PROF`) has the main thread on a processor almost all the time, about three
    quarters of it in three polling loops: 0x3E8910 in frame routine 0x3E82A8 (reload a halfword flag, 16 nops,
    loop, ~13%), and the job-slot scan 0x416338 called from loops 0x416654 and 0x416A44 (~25%), plus a bounded
    poll at 0x464F80 (~15%, not hooked). A probe called a hook at the three loop heads that paused for the first
    64 quick repeats and then called `SwitchToThread`. Game frames/s over 360-720 s, one exe, env alternated
    y n n y y n (x100): 11.42 / 12.02 / 10.39 yielding against 12.53 / 12.00 / 11.53, no hang. The pair with no
    other load on the host is even; in the other two the yielding run had a browser or chat client using ~0.8-1.1
    cores and lost more. The game used ~7 logical processors in both arms. With the pinning result, two tests say
    host CPU contention is not what limits frames/s; the spin cycles freed did not turn into SPU progress. The
    probe was removed.
- **M0.78: MFC parameter channel writes skip the MFC path, +1-2.7% (2026-10-27).**
  - A host profile of the critical render/job chain (x91, SPURS workloads 2 and 9-13) puts the runtime at ~36% of
    samples, with ~18% in channel/MFC code (`spu_wrch_impl` 3.3%, `mfc_for` 2.5%, `mfc_submit` 2.2%) and ~14% in
    indirect-branch dispatch (`spu_indirect_branch` 6.6%, `spu_lookup` 3.9%, `spu_drain_call` 3.8%).
  - Five or six of the eight or nine channel writes of every DMA only latch LSA, EAH, EAL, size, tag or the tag
    mask. `spu_wrch` now stores those in the context directly instead of going through `spu_wrch_impl`,
    `mfc_for` and the channel switch (toolkit b0c9b2c8, c1720f75). `SPU_CHHIST` turns it off.
  - The first A/B (x92) measured nothing: the fast path was also off under `SPURS_PM_PROF`, which the A/B harness
    always sets. It did show the noise floor: the two M0.77 runs differed by 0.44 game frames/s (11.66 / 11.22).
  - Game frames/s over 360-720 s, alternating, one instance at a time (x93): 12.06 / 11.84 against 11.74 / 11.72
    for M0.77. SPU busy time 391.3 / 358.3 against 397.0 / 391.3 ms per frame (window totals 91.6 / 93.5 against
    94.5 / 94.7 s), drain 81.5 / 71.9 against 77.8 / 76.2. No hang. The CPU log shows nothing else above 13 CPU-seconds per 30 s during the runs.
  - Both arms ran with `SPURS_PM_PROF=60`, which times every channel write with two `QueryPerformanceCounter`
    calls in the old build; the fast path returns before that. Without `SPURS_PM_PROF` (normal play) the saving is
    smaller than measured. The A/B metrics come from `PS3_FTRACE` and the heartbeat, so later channel-path A/Bs run
    with `NOPMPROF=1`.
  - The main thread waits on the SPU chain for 71-82 ms of each 76-87 ms frame, so only work that shortens that
    chain shows up in frames/s; a trim smaller than ~3% cannot be told apart from noise with two pairs.
- **M0.77: GETLLAR without the lock-line lock for registered holders, +0.5-2.6% (2026-10-26).**
  - The last v0.18 SPU commit held back from M0.76 (c9a450d8): a context that already holds a reservation and
    re-reads a line that is already marked publishes its reservation and copies the line without taking the
    lock-line lock. Line marks are never cleared, so every repeated policy-module poll of the SPURS instance takes
    this path (no counter was added; read from the code).
  - Game frames/s over 360-720 s, alternating, one instance at a time (x89): 11.73 / 11.71 against 11.43 / 11.65
    for M0.76. The second pair is inside the spread of two M0.76 runs (0.22). SPU busy time is 10 ms per frame lower
    in both pairs (410.6 / 390.6 against 421.0 / 400.9), drain 82.0 / 77.3 against 85.1 / 79.9. The CPU log shows
    nothing else above 13 CPU-seconds per 30 s during the four runs.
  - It changes lock behaviour, so it also ran 25 minutes on its own (x90): no hang, 11.6-11.9 game frames/s per
    two minutes from 720 s on, working set 1127 -> 1156 MB and private bytes 2394 -> 2462 MB between 300 and 1500 s.
    From 240 to 600 s it ran at 10.0-10.4 while I was running git cherry-picks in the toolkit tree; the CPU log
    shows no other process above 8 CPU-seconds per 30 s then, so the cause is not confirmed.
- **M0.76: ps3recomp v0.18 SPU and runtime work picked onto the toolkit, +5% (2026-10-26).**
  - ps3recomp v0.18.0 is out (2026-10-26). Our 56 toolkit commits conflict with it in about 140 hunks over 26
    files, so the full merge waits; the commits that matter for speed were picked onto `gow-v017` instead:
    poison scan only when reported (a903520e), lwmutex with an atomic lock word (dc37f00b), a leaf-import fast
    path (9a3fbffd), buffered stdout/stderr (c40829d7), the walker forgetting a park (3e9311c6), a bounded EA->IO
    lookup (06a37689) and SPU_DIRECT (#510): a lifted branch with a known target tail-calls it instead of
    returning to the drain loop.
  - The SPU_DIRECT commit also makes a drain restore the outer `drain_ret_pc` when it returns. Here that hung the
    level after about 2.5 minutes (`[pm-hang] wid=12 pc=0x02EA0 img=10`, RenJobSet's policy module) with or without
    SPU_DIRECT on; this toolkit's interpreter still stops at `drain_ret_pc`, which v0.18 changed. Setting it back
    to 0 when a drain returns (3c818525) fixes it.
  - Lifts made before this need the new tails: `python scripts/spu_direct_rewrite.py port/spu --apply` rewrites
    33,953 sites in 364 images (a fresh lift with the toolkit's `spu_lifter.py` emits them already).
  - Game frames/s over 360-720 s, alternating, one instance at a time (x88): 11.41 / 11.51 against 10.91 / 10.90.
    Every per-minute rate of the new build is at or above the old build's. SPU busy time drops from 450-465 to
    400-419 ms per frame over six SPUs. No hang in four 12-minute runs. The CPU log was not running for this
    batch.
  - Held back: the GETLLAR fast path (c9a450d8) was picked, then reverted while the hang was bisected; it was not
    the cause and is M0.77. Per-line PUT copies (13a7676b) would break the PUT ordering this
    game depends on (`SPU_PUT_HEAD_LAST`).
  - `PS3_SPU_DIRECT=0` goes back to the trampoline.
- **M0.75: the GPU depth path pays with six kernel threads, +4% (2026-10-26).**
  - With the sixth kernel thread the main thread waits on the walker again, so `RSX_Z2ARGB=1` (M0.73) was
    measured again and is now the port default.
  - Game frames/s over 600-960 s, one instance at a time: on 11.06 (x81) and 10.75 (x82), off 10.33 and
    10.31 (x82, run first and third). The walker's sync GPU wait drops from 7-10 to 4.5-5.6 ms per frame, and
    drain from 85-98 to 77-86. Frames over 150 ms in the trace window are no more frequent with it on.
  - Four more runs were slow in stretches (per-minute rates down to 7-8) and are discarded. For two of them the
    CPU log shows other work on the host at those times: a WMI query of my own (x81n_2) and another session's
    long `grep` (x82z_2). For x81n_1 and x81z_2 the CPU log was not running.
  - `RSX_Z2ARGB=0` goes back.
- **M0.74: a sixth SPURS kernel thread, +1-2% (2026-10-26).**
  - SPU 4 was the last SPU without a kernel thread; HighestPrio6 and General6 have nonzero priority there. The
    port now sets `SPURS_KERN_SPUS=0,1,2,3,4,5` (was `0,1,2,3,5`).
  - Alternating, one instance at a time, game frames/s over 600-960 s (x80): 10.59 / 10.61 against 10.38 /
    10.49. Per frame: HighestPrio6 wait 11 -> 9 ms, flip pacing (`375A88`, main-thread profile) 7-8 -> 10 ms:
    the main thread waits on the walker again. SPU busy 420-447 -> 456 ms per frame over the six. No hang,
    frame dumps unchanged. Eight threads are busy on a 6-core/12-thread host.
  - Per workload, SPU busy per frame (x78n_1, five threads): General6 69 ms, the audio workload
    (`BrBDblBufWrkld`, SPU 5 only) 60, each EDGE lane 58, RenderJobs 32, BatchJobList 18, RenJobSet 11/12
    18/12. The audio share is a fixed ~67% of wall time on its SPU, not per frame.
  - `SPURS_KERN_SPUS=0,1,2,3,5` goes back.
- **M0.73: depth read as A8R8G8B8 built on the GPU: exact, but no frame-rate change (2026-10-26).**
  - The largest `z2guest` case is the 1280x720 D24S8 zeta at `0x011B0000`, sampled twice per frame as a linear
    A8R8G8B8 texture (the deferred passes rebuild positions from it). The walker stored it to guest memory (a GPU
    wait, a CPU conversion, an upload) each time. No CPU or SPU reads those bytes: an SPU GET watch over the
    region and a PPU read watch on the zeta saw none.
  - Toolkit `RSX_Z2ARGB=1` builds the texture on the GPU instead. A compute shader packs the depth and stencil
    aspects into the texels the guest word decodes to, and the result is cached until the depth changes.
    `RSX_Z2ARGB=2` also does the store and compares: 0 differences over ~17k compares in a 12-minute run (x78chk).
    The first version differed only where depth was 1.0, because the NVIDIA driver reassociated the shader's
    rounding (0x1000000 instead of 0xFFFFFF). The math is now `precise`.
  - `ENG_PHASE_STATS` (x79z against x76prof): z2guest falls from ~75 to ~0.5 ms per second of play, and texture
    resolve from ~230 to ~150 ms per second. In the frame trace, the sync GPU wait goes from 7.3-8.5 to 5.3-5.5 ms
    per frame.
  - Game frames/s over 600-960 s: 10.40 on against 10.48 and 10.35 off (x78). The second "on" run is discarded
    because it overlapped my own trace analysis (9.46, with a 3-minute dip that matches the analysis).
  - The frame trace explains the null result. The SPU rows are solid while the RSX row has idle gaps, and the
    walker is busy ~70% of the time with or without the change. The frame is SPU-bound, so the walker time saved
    only waits longer.
  - With it on, 8-11 frames over 150 ms appear at 613-618 s, in all three traces, against 0-1 off. In those
    frames the RSX row is a GPU wait. The new path itself takes at most 4 ms a call and created 2 textures in
    the whole run, so the cause is not yet known.
  - Off by default (the port does not set it). Next: the SPU side, the runtime's own dispatch cost (~20-29% of
    SPU time).
- **M0.72: a fifth SPURS kernel thread, +4-11% (2026-10-26).**
  - With three EDGE lanes the four kernel threads were ~100% busy (x71: SPUs 99-103% in the trace), and the
    main thread's two SPU waits were the frame's slack: the EDGE list (`3E8388`, ~7 ms per frame) and
    HighestPrio6 (`467B2C`, ~14). The port now sets `SPURS_KERN_SPUS=0,1,2,3,5` (was `0,1,2,5`): SPU 3, where
    EDGE0-2 have priority 8, HighestPrio6 2 and General6 12.
  - Alternating, one instance at a time, game frames/s over 600-960 s (x74, x75): 9.77 / 10.02 / 10.12 against
    9.37 / 9.01 / 9.09. Per frame: EDGE wait 7 -> 0 ms, HighestPrio6 wait 14 -> 11, flip pacing (`375A88`)
    6-7 -> 10-13 (the main thread now waits on the walker). Seven threads are busy on a 6-core/12-thread host;
    the walker's drain grew in one pair (115 against 93 ms) and not in the other two. Frame dumps unchanged, no
    hang.
    `SPURS_KERN_SPUS=0,1,2,5` goes back.
  - The heartbeat's `presents` are not distinct frames: `flips` equals `game_frame` in every run, while presents
    run 1.4-1.8x higher. The frame clock (`main.cpp`) presents on the flip-pending flag and again when the flip
    count changed, so one flip can be presented twice. Game frames/s is the frame rate; earlier presents/s
    figures in this file count those repeats.
  - Next: the walker is the bound again. Its largest known cost is depth and colour targets sampled with
    another format, copied to guest memory and uploaded again (`z2guest`/`s2guest`/reupload in
    `ENG_PHASE_STATS`, ~19 ms per frame).
- **M0.71: EDGE on three SPUs at once, +65%; the 3D scene the right way up (2026-10-25).**
  - Parallel EDGE lanes are on by default: the port sets `SPURS_LANES=15,16,17=3` (toolkit `SPURS_LANES`, see
    the lanes entry under M0.62 for how it works and why the old blocker was our runtime). One instance at a
    time, 600-1080 s, alternated (x63): off 5.63 / 6.20 game frames/s, two lanes 8.41 / 9.45, three lanes
    9.65 / 9.76; presents/s 11.05 / 12.28 off, 13.51 / 13.72 with three. No `[pm-hang]` or HALT in any run, frame
    dumps clean. `SPURS_LANES=off` goes back to one EDGE lane.
  - The 3D scene was upside down under an upright HUD (seen in the frame dumps). The fragment translator read
    WPOS as the host position (origin top left) and ignored `SET_SHADER_WINDOW` (0x1D88); GoW sets origin BOTTOM
    and builds its full-screen pass UVs as `wpos * (1/W, -1/H) + (0, 1)`, so every such pass mirrored its
    source. WPOS now follows the window register (height when origin BOTTOM, pixel-center bias), passed in the
    unused lanes of `fp_alpha`. Check run x64 (both defaults on): scene upright with the HUD upright, no hang,
    9.64 game frames/s and 13.9 presents/s at 600-1080 s. 127-174 of the game's fragment programs read WPOS,
    so screen-space reads in materials changed too; nothing wrong seen in the dumps, not checked further.
  - **Where the frame goes with three lanes** (x65: production defaults, one instance, 1100 s, PM profiling on,
    `PS3_FTRACE=600:20`), against x56 (six stages, one lane). Tail 5.95 -> 9.06 game frames/s (168 -> 110 ms
    per frame). The main thread's EDGE list wait (`3E8388`) fell from 83 to 6 ms per frame (49% -> 5% of
    samples): EDGE no longer bounds the frame. EDGE w15-17 busy 160% of wall (lanes overlap); ms/job
    0.0995 -> 0.1105 (+11%: the lanes share host cores). Timeline 600-620 s: 166 frames, median 112.8 ms; the
    RSX walker's drain is 110.5 ms per frame (of which GPU waits 9.4), i.e. the walker runs the whole frame,
    and the main thread is blocked only 17.7 ms per frame (flip pacing `375A88` 11.7). The frame is now bound
    by the walker's CPU time (~101 ms) and the main PPU thread (~95 ms busy) together: speeding up only the
    walker would land the frame on the main thread at ~100 ms. The four kernel threads (SPUs 0, 1, 2, 5) show
    100% busy; whether EDGE lanes spin at the ring-space wait is not known yet. The overlay stage O11430 stays
    default-lifted: with EDGE no longer the bound, an A/B of its ~1.9% of job time would measure noise.
  - **Host profile with three lanes** (x66: `PS3_SAMPLE=2 PS3_SAMPLE_HOST=1`, 1100 s; resolved with
    `samp_resolve.py`). The lanes do not spin at the EDGE ring-space wait: no samples at `B700`/`409C` on any
    kernel thread, `mfc_do_transfer` 1.7%. Six threads are busy nearly all the time on the 6-core host: main
    20.3% of samples, the four kernel threads ~16% each, the walker 13.1%.
    - Main thread: `vm_write64` 18.1% (the out-of-line store in `ppu_loader.cpp`; `vm_write32` is 0.5% of the
      whole process), guest `466E38` 14.8%, `416338` 14.7%, `416610` 8.7%, `3E82A8` 6.4%, `ppu_trace_enter`
      4.6% (the lift was made with `PPU_TRACE_FUNCS=all`).
    - Kernel threads: job module `a67e` `4AB0` ~9% each (not EDGE: absent from cap_3), runtime dispatch
      (`spu_indirect_branch`, `spu_drain_call`, `spu_lookup`) ~20%.
    - Walker: `rsx_vertex_remap_build` 18.1%, `rsx_draw_engine_guest_read` 10.8%, texture content hashes ~8%.
  - **How to measure from here.** With the walker (~101 ms CPU) and the main thread (~95 ms busy) both near the
    frame, a change on one side moves frames/s by at most ~12% before the other side bounds it. A one-sided
    change is gated on its own thread's cost from one ftrace window: walker CPU = drain - GPU waits, main busy
    = frame - main blocked (plus `VK_DRAW_PROF` us/draw for the walker); frames/s is claimed only when both
    sides have come down.
  - **Inline lifted stores and a gated trace hook** (toolkit `ppu: inline fast path for lifted stores`). Every
    lifted store called the out-of-line `vm_writeN` (the lift's loads were already inline); the lift header now
    stores inline when no store diagnostic is armed (`vm_inline_st_ok`, from the variables that arm the
    watches and store observers), the address is plain guest memory and its line is not SPU-reserved, and
    otherwise calls the out-of-line store as before. `ppu_trace_enter` runs only while it can still log
    (`ppu_trace_live`). The existing lift (`work/recompiled_v015`) was not re-lifted: its `ppu_recomp.h` was
    replaced by the new template preamble plus its original forward declarations (the template's `vrsave`
    field left out, as the v015 sources were lifted without it). x68 against x65 (one run each, same setup):
    frame median 112.8 -> 106.1 ms; main thread busy (frame - blocked) ~95 -> ~88 ms; walker CPU (drain - GPU
    waits) 101.1 -> 95.8 ms; 600-1080 s 9.06 -> 9.27 game frames/s, 14.25 -> 14.40 presents/s; no hang.
  - **Walker per-draw work** (toolkit `rsx: cheaper per-draw walker work`; switches in DEBUGGING.md, all default
    on). Baseline per 10 s (`ENG_PHASE_STATS`/`VK_DRAW_PROF`): ~170k draws; per draw fetch ~9 us, pipeline
    ~5.5, textures ~15, backend ~10.5. Causes found: the vertex remap probed a hash table sized for the largest
    draw ever seen; the guest readback counted non-zero pixels for the pass dump even with no dump running;
    the pipeline key re-hashed the vertex program byte by byte every draw; the texture content hash was one
    serial multiply chain. Fixed without caching anything across guest writes (no write tracking exists).
    x69 (on) against x69off (all four off), same exe, one run each: walker CPU (drain - GPU waits) 88.5 vs
    96.2 ms per frame (x68 baseline 95.7); main thread busy 89.8 vs 86.5; frame median 105.3 vs 105.0 ms. The
    frame did not move: it is now bound by the main PPU thread (~87-90 ms busy). Frame dumps unchanged; no
    hang. Still on the walker: depth/colour stored to guest memory and re-decoded as textures (~18 ms per
    frame with GPU waits), FIFO decode (~16% of walker samples), `rsx_vertex_fetch_many` (6.4%).
  - **The main thread's HighestPrio6 wait is SPU work, not scheduling** (x70, x71; one instance, 3 lanes,
    `PS3_FTRACE=600:20`). The top main-thread guest function `466E38` (called from `467B18`) busy-waits on the
    per-SPU progress counters of the `HighestPrio6` job list (wid 2, priority 3 3 3 2 1 1): ~13-15 ms per frame,
    counted as main busy. Two scheduler changes left it unchanged:
    - `SPURS_RERUN_YIELD=1` (toolkit, default off): end a module's rerun window when a workload the SPU ranks
      higher is ready. It fired (~10k ended reruns a minute) and changed nothing: x71 game frames/s 600-1080 s
      9.27 against baseline 9.36 / 9.34, HP6 wait 15 against 14 ms per frame.
    - `SPURS_PM_RERUN_US=500` instead of 2000: 9.30 / 9.34 game frames/s, HP6 wait 14 ms.
    - The trace shows why. HighestPrio6 runs once per frame, at the end (median start 99 ms after the flip,
      7 spans, 2.1 ms busy, one cluster on one SPU). In the 15 ms before it starts the four kernel threads are
      busy throughout: General6 (wid 3) 11.8 ms, RenJobSet (wids 11, 12) 16.2, EDGE (15-17) 22.2, audio (8) 7.7.
      The list becomes ready only after the SPU work it depends on, and the kernel threads are ~100% busy.
    - SPU busy time by workload (x71 baseline): General6 17%, audio 16%, EDGE 14% each, RenderJobs (13) 8%,
      RenJobSet (11) 4%, BatchJobList (7) 4%. The next lever is SPU compute (General6 first), not the kernel.
    - 2 EDGE lanes instead of 3 (x70): 8.71 / 8.87 against 9.30 / 9.37 game frames/s (EDGE wait 15 against
      7 ms per frame). 3 stays the default.
    - Noise: stretches of a few minutes run ~15-20% slower on every thread at once (EDGE ms/job 0.122
      against 0.108, walker drain 125-128 against 85-98 ms), whatever the settings, 40 minutes to 2.5 hours
      apart (x70, x71, x73, x74; e.g. the 30 s heartbeats of x73w_2 drop from ~9.3 to 7.5-8.4 game frames/s
      between 840 and 1050 s only). Likely a host task; a run that overlaps one is flagged from its heartbeats
      and compared over the clean window, not discarded.
  - **What the HighestPrio6 list waits on** (x71 baseline trace, x72 host samples per workload with
    `PS3_SAMPLE_WIDS`). Per frame, the last span of RenJobSet wid 11 ends a median 0.9 ms before HighestPrio6
    starts and it does not run again until the next frame; General6 (wid 3) ends 2 ms before but keeps running
    through it, so it is not the gate. The RenJobSet lists run as a chain at the end of the frame: wid 9
    (ends ~29 ms before HighestPrio6), wid 10 (~22), wid 11 (26.6 ms wall, 17.2 ms busy) and wid 12 (12.6 ms
    busy in 9 long jobs, ends ~4 ms before).
    - Wid 11 runs as 9 visits of ~2.06 ms (the rerun window) with ~0.86 ms between them, 95% of them on a
      different kernel thread: ~7.4 ms a frame waiting to be picked again, while the SPUs stay full (42.7 of
      44.2 SPU-ms busy in those gaps: General6, EDGE, audio).
    - `SPURS_PM_RERUN_WIDS="11,12=10000"` (toolkit, default unset; x73, alternating, two runs each): wid 11's
      chain 21.0 / 21.0 ms against 27.5 / 25.9, but it starts later by about as much. HighestPrio6 wait 15 / 15
      against 14 / 14 ms per frame, game frames/s 9.23 / 8.74 against 9.22 / 9.30, EDGE wait 9 against 7 ms.
      The chain itself is gated by earlier work, not by its pickups.
    - Host samples while each workload runs: General6 is 61% `jobmod_a67e` `4AB0` and 15% `63B4` (one
      routine; native-stage candidate, ~13% of SPU time). RenJobSet is flat: no function above 6%, ~29% in
      the runtime (`spu_drain_call` 10%, `spu_indirect_branch` 4%, MFC and channels).
    - With the kernel threads full and the end-of-frame lists chained, scheduling knobs are done. What is left
      is SPU compute: General6's `4AB0` as a native stage, and the runtime's dispatch cost on every workload.
- **M0.70: the sixth native EDGE stage, +2.7%/+3.8% (2026-10-25).**
  - The loop nest of EDGE job function `6AC8` (`loc_6BE0`/`loc_6DE0`, ~3.7% of job time: a smooth falloff
    around up to three spheres per group of 8 gathered vertices, accumulated and stored by index) as AVX2
    (`port/gow_edge_native_6ac8.c`). Bit-exact: cap_3 replay 400/400, and an in-game check of 1355776 calls
    against the lifted loop nest with 0 differences, recompute and v1 paths included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first five stages: +2.7% and +3.8% game
    frames/s. Default on; `GOW_EDGE_6AC8=0` restores the lifted loop nest. DCC0 (4.4%) was probed and not
    staged: a serial integer decode with nothing to pack.
- **M0.69: the fifth native EDGE stage, +2-3% (2026-10-25).**
  - The loop of EDGE job function `F538` (`loc_F620`, ~5% of job time: a 4x4 matrix transform of each input
    quad, mostly in place) as hand-written AVX2, two quads per vector (`port/gow_edge_native_f538.c`).
    Bit-exact: cap_3 replay 400/400, and an in-game check of 2392064 calls against the lifted loop with 0
    differences, recompute and fallback paths included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first four stages: +2.3% and +3.3% game
    frames/s (4.10 -> 4.21). Default on; `GOW_EDGE_F538=0` restores the lifted loop.
- **M0.68: the fourth native EDGE stage, +5-6% (2026-10-25).**
  - The loop of EDGE job function `A880` (`loc_A950`, ~7% of job time: per element normalize(B x A * B.w),
    plausibly a bitangent) as hand-written AVX2, 8 elements per vector (`port/gow_edge_native_a880.c`).
    Bit-exact: cap_3 replay 400/400, and an in-game check of 1736704 calls against the lifted loop with 0
    differences, fallbacks included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first three stages: +5.6% and +5.0% game
    frames/s (3.98 -> 4.19). Default on; `GOW_EDGE_A880=0` restores the lifted loop.
- **M0.67: the third native EDGE stage, +3-4% (2026-10-25).**
  - The vertex transform loop of EDGE job function `D250` (`loc_D4DC`) and the triangle culling loop of its
    caller `D640` (`loc_D92C`, index compaction), ~10% of job time together, as hand-written AVX2, two
    iterations per vector (`port/gow_edge_native_d640.c`). Bit-exact: cap_3 replay 400/400, and an in-game
    check of 917504 runs of each loop against the lifted loops with 0 differences (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first two stages: +2.9% and +4.2% game
    frames/s (3.88 -> 4.02). Measured alone (one instance, profiled), the cost per EDGE job fell 8.4%, more than
    the replay's -6.7%: the paired A/B understates the gain (docs/EDGE_NATIVE.md). Default on;
    `GOW_EDGE_D640=0` restores the lifted loops.
- **M0.66: the second native EDGE stage, +8% (2026-10-25).**
  - The light-accumulation region of EDGE job function `73E0` (`loc_79B0`..`81C0`, 76% of 73E0, which is
    ~19% of job time) as hand-written AVX2, 8 vertices per vector (`port/gow_edge_native_73e0.c`, v3).
    Bit-exact: cap_3 replay 400/400, and an in-game check of 1146880 region runs against the lifted region
    with 0 differences, fallbacks included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B, same exe, on top of the E550 stage: +7.8% and +7.9% game frames/s (3.60 -> 3.88).
    Default on; `GOW_EDGE_73E0=0` restores the lifted region.
- **M0.65: the first native EDGE stage, +8-9% (2026-10-25).**
  - The skinning loop of EDGE job function `E550` (`loc_EC08`, 75% of E550, which is ~15% of job time) runs as
    hand-written AVX2 (`port/gow_edge_native.c`, v3): two vertices per 256-bit vector, SPU float rules checked
    once per iteration, with an exact recompute when a lane fails. Bit-exact: cap_3 replay 400/400, and an
    in-game check of 2252800 loop runs against the lifted loop with 0 differences (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B, same exe: +8.0% and +8.8% game frames/s. Default on; `GOW_EDGE_NATIVE=0` restores the
    lifted loop.
  - The loop body the native code reuses is game code: it is generated from the lift at build time, not
    committed.
- **Parallel EDGE lanes (beyond hardware) -- assessed, not started (2026-10-24).**
  - The game registers every workload, EDGE included, with contention min 1 max 1 (now printed at registration):
    on the console each EDGE workload runs on one SPU at a time. Our `SPURS_MAXCONT` lanes run one after another in
    one kernel thread, so they add no parallelism.
  - EDGE priority is `8 8 8 8 0 0 0 3` on a 6-SPU instance: at most 4 lanes (SPUs 0-3), with real spuNums, because
    the WWS sync rows sit at `+0x40+16*spuNum`.
  - Blocker: EDGE job code updates the shared allocator line `0x83257500` with GETLLAR -> compute -> unconditional
    PUTLLUC (pcs `0x424C`/`0x6988`/`0xC760` -> `0x448C`/`0x6724`/`0xC910`). Two lanes would lose updates.
  - Plan, if this is pursued:
    1. The runtime holds a line's reservation from a context's GETLLAR until its next PUTLL*.
    2. One wid, two lanes: a per-wid run count, a distinct spuNum and a persistent PM context per lane, and more
       kernel threads.
    3. Correctness first: no `[pm-hang]`, no texture garbage, every EDGE list completes.
  - The soak's present rate is ~7.1 presents/s (flips ~3.7/s, which is what "game frames/s" counts here).
  - **Re-assessed 2026-10-25: the blocker above was our runtime, not the game.** The allocator line's EA is
    `0x83257480 + 0x80 + 0x80*spuNum` (spuNum from kernel LS `0x1C8`); `0x83257500` is spuNum 0's line, and every
    EDGE run here got spuNum 0 (`spu_run_policy_module(..., 0)` on the default single-lane path). A guest peek
    (x59, `GOW_PEEK` on the five lines) shows the game initialized the lines for spuNum 1-3 with their own
    disjoint rings and labels (main rings `0x83517880`, `0x837D7880`, `0x83A97880`, labels `0xC90`/`0xCA0`/`0xCB0`,
    second rings `0x819C6300`/`0x81A26300`/`0x81A86300`), untouched during play. Lanes that run with distinct
    spuNums use separate lines and rings: no lost updates, and each ring keeps one producer. The `SPURS_MAXCONT`
    path already gives lanes 1..N-1 spuNums 1..N-1; its measured slowness was a host thread per lane per dispatch
    plus `SPURS_PM_RERUN_US` being skipped when maxcont > 1. Job outputs are found by the offsets each job writes
    into its own command segment, so completion order between lanes is free (read-only study of the d935 lift
    and cap_3). Still open: spuNum must be unique across all concurrent EDGE contexts, not only within one wid;
    and whether the GETLLAR/PUTLLC reservation is safe across host threads (its "real lock-line semantics" code
    predates the 2026-07-23 LBP note about two threads racing on a jobIndex CAS).
  - **First lanes runs (toolkit `SPURS_LANES`, 2026-10-25).** Up to n kernel threads may claim one workload at
    once (`s_wid_busy` is a count for those wids), each running the module as the SPU its thread plays, so each
    lane has its own spuNum, PM context and allocator line; no thread is created per dispatch and
    `SPURS_PM_RERUN_US` keeps working per lane. x61 (`15,16,17=2`) froze at ~150 s: the thread playing SPU 5 ran an
    EDGE lane as spuNum 5, whose line the game never set up (EDGE priority is 0 there), and spun at the
    ring-space wait `0x409C`. Lanes now run only on SPUs the workload has a priority on (0-3 for EDGE). x62 (same,
    1100 s, one instance, no PM profiling): no `[pm-hang]`/HALT, the cursors of lines 0, 1 and 2 all move
    (~16.5k changes each), `[edge-diag]` another EDGE wid busy 15-16%, frame dumps clean. t 600-1080 s: 9.08
    game frames/s and 13.96 presents/s, against x56's 5.95 and 11.85 (x56 had PM profiling on; one run each).
    The alternated single-instance measurement follows.
  - Same method on ucrtbase (3.4% of EDGE samples): 97% was `getenv`'s environment walk. The sampler now unwinds such
    samples to their exe caller; all of them came from `spu_run_with_halt`, which read `SPU_ARGWATCH` after every
    module run. It is read once now. The A/B was neutral within noise (runZg vs runZe: -3.2% / +0.4%, EDGE ms/job
    equal), since that cost sits outside the measured job time. Kept: it is a plain per-run cost.
  - What remains on EDGE outside lifted code:
    - the dispatch of the policy module's own functions through its call table (`0x1BA4`/`0x1BE8`/`0x1CC0` from
      `0x2E8C`, ~5k/s each per workload);
    - the generated stubs;
    - `spu_drain_call`.
- `level_rate.py` now tolerates heartbeats that drift off the 30 s grid.
- A fifth pair (x13) also ran clean: 0 of 5 pairs hung with the fix. `VK_SLOW_MS` now also counts live
  `VkDeviceMemory` objects. They peak at ~1094, which is the 1024-entry texture cache full plus targets and rings.
  Slow `vkQueueSubmit` calls happened with as few as 17 alive, so the allocation count alone does not explain
  them. What is still slow adds up to about 6% of a run: image frees from cache evictions (251 calls, 28.6 s),
  submits (67, 21.6 s) and fence waits.
- Upstream: the fix is PR #552 (ported to `master`, which has no batching). PR #532 (`VK_DRAW_BATCH`) failed CI on
  shutdown leaks under the validation layer. A reviewer sent the fix, which is now on the PR branch with their
  authorship. The same leaks (batching rings and the depth-snapshot bounce buffer) are fixed in this fork's toolkit.
- Process Lasso's ProBalance on the test machine can lower the game's priority mid-run. Benchmark with it closed.

**Open bug: texture garbage in the level.** Some meshes and backgrounds sample tiled, multicoloured noise, which
looks like texels read from memory that does not hold the texture (yet). Examples: `vos_1` f_08400 with batching
off, `ubn_2` f_09600-f_11400, and before the M0.51/M0.52 toolkit changes `rge_1`/`rge_2` f_06600 and `rg_1` f_07200
(the worst case, which also hits the HUD). So it is older than this work and varies run to run. A cheap detector over
the dumps: the share of pixels whose colour differs from the pixel 8 to the right with a large chroma change
(> 90). That share is about 0.002-0.006 in clean level frames and 0.012-0.05 in garbage ones; menu frames give
false positives. The noise follows the mesh UVs and comes in blocks: the texture went through the normal
upload path with the wrong guest bytes.
- Ruled out:
  - NV3089 blits sent to the swizzled NV309E destination by the size guess. The title only ever binds context
    surface `0x313371C3` (SURFACE2D, linear), and routing by that handle never disagreed with the guess in 2 runs.
  - Textures that sample an old render target by offset with a compressed format or a larger size than the
    surface: 0 hits in 2 runs (`ENG_ALIAS_DBG`), both of which had garbage.
- Rate: in runs that reach f_09000, garbage started by then in 1 of 19 runs on the M0.51 builds and in 6 of 22 from
  the M0.52 build on. That includes a batching-off run as slow as the old ones. The decrementer change (TSC
  instead of QPC) is the suspected amplifier, under test (`SPU_HOST_NS=qpc`).
The port turns it on (`main.cpp`); `VK_DRAW_BATCH=0` goes back to a submission per draw. Next in the same timeline:
depth snapshots (527 x 4.8 ms) and texture uploads (2992 x 0.35 ms) still submit and wait on their own.

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.13): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
