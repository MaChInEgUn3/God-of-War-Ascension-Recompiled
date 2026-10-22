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

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.13): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
