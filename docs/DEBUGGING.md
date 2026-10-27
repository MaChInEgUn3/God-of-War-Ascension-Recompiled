# Debugging playbook

Lessons that cost days; keep them.

* **Game asserts are `twu r1,r1` traps and the lifter ignores them.** The first red line in the TTY is the first real
  assertion; everything after it is garbage state. Look for the *first* anomaly.
* **stdout and stderr interleave unpredictably** (HLE `printf` vs TTY). Never infer ordering from a merged log.
* **Function-entry tracer**: `PPU_TRACE_FUNCS=375754,3754a8 python ppu_lifter.py ...` (workers inherit the env), then
  run with `PPU_TRACE_MAX` and `PPU_TRACE_DUMP=<fn>,<reg>,<len>`. Re-lifting costs about 35 s, a full rebuild about 7 min.
* **Static analysis**: `analysis/ppudis.py <from> <to> [elf]` (capstone; maps addresses through the ELF program headers of the decrypted `EBOOT.elf`) and `analysis/findstore.py <ea>` (finds `lis`+displacement
  reads and writes of a global). capstone prints `add/subf/addi` as `addc/subfc/addic`; the lifter works from encodings.
* **Host-stack offsets in `[GCS:...]` call chains are not guest offsets**; they only name the functions involved.
* **Memory watch**: `GOW_GUARD=<hex ea>` arms the page guard (reports the writer function and chain).
* **GCM**: `GCM_MEMTABLE=970F50` dumps the title's mapping table and allocator list at each map/unmap;
  `GCM_UNMAP_STACK=1`, `GCM_UNMAP_SCAN=<hex,hex>`, `GCM_UNMAP_DUMP=1`.
* **SPU spins**: `SPU_SPIN_N=<n>` lowers the GETLLAR spin report threshold (default 4M) and dumps the code around pc + r0..r15; `RSX_SEMLOG=1` logs RSX semaphore/label methods.
* **SPURS**: `SPURS_PM_PIPE=1` (WWS pipeline counters; idle runs only first 8 per wid, active runs up to 3000), `SPURS_KEEP_READY=1` (kernel does not consume readyCount), `SPURS_WID_STATS=1` (runs per workload), `SPU_IRQTRACE=1` (SPU interrupts; nested takes dump the runtime-built shim), `SPURS_ZLOG=1` (every Edge Zlib inflate element, raw words), `SPURS_TRACE=1`, `PS3_SCTRACE=1`, `GOW_LLE_DUMP=<s>` (live CellSpurs), `GOW_LLE_WATCH=1`, `PS3_HLE_ARGS=<nid>`.
* **Editing gotcha**: in tool-driven Python heredocs, an escaped newline inside a C string can turn into a real
  newline. Compile after every such edit.
* **Who waits for whom (timeline)**: `PS3_FTRACE=<start s>:<seconds>:<path>` records PPU blocking, SPU channel waits, the RSX walker's drains and (Vulkan) every synchronous submit as a GPU_WAIT span (a=3, b = caller RVA; resolve against the link map), then `tools/ftrace_report.py <path>` in the toolkit. `[pm-prof-ch]` (with `SPURS_PM_PROF`) splits each busy workload's channel-read time by channel; `SPU_DECPC=<n>` prints which host return addresses read the decrementer most (a spinning backoff shows as one site).
* **Draw batching (Vulkan)**: `VK_DRAW_BATCH=1` (port default) records engine draws into one submission, with up
  to `VK_DRAW_BATCH_MAX` (default 512, max 1024) per batch. `VK_DRAW_BATCH=0` goes back to a submit and fence wait
  per draw, which is the first thing to try when a rendering bug might be an ordering one. With it on, depth
  snapshots, texture uploads (64 MB staging ring) and clears are recorded into the same batch; only readbacks and
  the present wait. The log prints
  "draw batching: on" once and then a draws/submissions count every 16384 flushes.
* **Run-time selectable tracer**: lift once with `PPU_TRACE_FUNCS=all`, then pick functions per run with
  `PPU_TRACE_ADDRS=3c93e8,2a0000-2b0000` (hex, ranges allowed) and cap with `PPU_TRACE_MAX`. The GoW:A port defaults `PPU_TRACE_ADDRS=0` (trace nothing). Each line shows
  `tid`, `r3..r7` and `lr`. `PPU_TRACE_DUMP=<fn>,<reg|0xEA>,<len>[,<hops>]` also dumps memory (a `reg` of 32 or more is an
  absolute guest address; `hops` follows pointers first). `PPU_TRACE_DUMP2` takes a second spec of the same form,
  so one entry can show an object and a block it points to. The `all` build is slower; relift without it for normal runs.
* **Guest memory probes** (port): `GOW_PEEK=<ea>,<words>[;<ea>,<words>...]` logs every change (20 ms poll; up to 8
  regions of 32 words; in Bash write `GOW_PEEK="a,n;b,m"` so the `;` is not a command separator); `GOW_SCAN=<hex bytes>`
  logs where a byte pattern appears. Both are host threads; times are ms since start.
* **GCM FIFO park**: `GCM_PARK_WAIT=1` (default in the port) keeps the walker on a self-JUMP park until the title
  patches it, instead of resyncing to `put` (`jump-park`/`stuck-get`). `GCM_RECDBG=1` prints every JUMP and a FIFO dump
  at each resync.
* **RSX semaphores**: `GCM_SEMA_ACQUIRE=1` (default in the port) blocks the walker on NV406E SEMAPHORE_ACQUIRE;
  with `GCM_RECDBG=1` every release and every acquire outside the `0x062xxxxx` ring is logged, plus one
  `ACQUIRE blocks ...` line per blocking point.
* **WWS / SPU tracing**: `SPU_WWSJOB=1` (job entries per wid, 40 each), `SPURS_PM_DMA_WID=<n>` (every DMA of
  workload n's PM runs), `SPU_DMA_IMG=<n>` (every DMA of SPU image n), `SPU_INTERP_STATS=1` (interpreter hand-offs and
  steps per image), `SPU_WAITLOG=1` (WAIT_SIGNAL exits; the enter line also shows `cpu=`/`lockwait=`/`atomics=`
  since the last exit, which separates compute from blocking).
* **Heap / memory corruption**: `GOW_HEAPWALK=<hex sentinel>[,...]` (port) walks a game heap's circular free list
  (size +0, next +4) every 50 ms and reports the first link that leaves it; `SPU_DMA_EA=<lo>-<hi>` logs every SPU
  PUT-class DMA overlapping a range; `GOW_GUARD=<ea>` then names the writer (`CLEARED` lines carry a guest
  backtrace); `PS3_NULL_READ_STACK=1` adds the guest stack to `[null-read]` (its `guest-fn` is the last traced
  function, not necessarily the reader).
* **Progress and profile**: `GOW_HEARTBEAT=<s>` prints `t`, the flip count and the game frame counter
  (`*(0x9710C8)`) every s seconds, so a run that never stalls still says how far it got. `SPURS_PM_PROF=<s>` prints
  each workload's share of wall time in policy-module runs (`w<wid>=<pct>%/<runs>`, and the total = how busy the
  single kernel thread is); `SPU_TASK_PROF=<s>` prints per task image the count, average time and share spent
  waiting for the taskset LS lock. `[pm-prof]` entries read `w<wid>=<pct>%/<runs>i<pct>%`, where the `i` part is
  the share of that workload's time spent in the SPU interpreter (runtime-loaded job code with no lift).
  `GOW_MAIN_PROF=<s>` samples the main PPU thread's guest call chain every 2 ms and prints the top chains
  (`ra <innermost return address> in <caller fn>  lr <live LR>`), i.e. where each frame waits. The live LR is the
  most recent call, inside a leaf or an HLE import the back chain cannot show. An `lr` right after a `bl` to an
  import stub (`0x628xxx`) means time inside that firmware HLE; `analysis/imp.py` plus the NID database name it.
  `SPURS_TASK2_PROF=<s>` times `cellSpursCreateTask2` (calls, share of wall, avg/max) and splits `CreateTask` into
  stages.
* **What the GPU drew (Vulkan, needs `PS3RECOMP_VK_GUEST_PROGRAMS=1`)**:
  * `PS3RECOMP_VK_DUMP=<file>` with `PS3RECOMP_VK_DUMP_EVERY=<n>` writes every n-th presented frame as PPM; a
    `%05u` in the name keeps a numbered sequence. `analysis/ppm2png.py` converts one frame;
    `analysis/sheet.py "<glob>" <out.png>` builds a 3x3 contact sheet.
  * `PS3RECOMP_VK_DUMP_SURFACES=<present#>,<dir>` writes every engine colour target at that present and logs its
    value range. A surface holding only its clear value means no draw reached it.
  * `RSX_ENGINE_PASS_DUMP=<dir>` (with `_AFTER=<s>`, `_MAX=<passes>`, `_DRAWS=1`) writes `passes.txt` (targets, zeta,
    depth/blend/stencil state per pass) and `draws.txt`, one line per draw with the viewport, vertex attribute 0
    (location/offset and first element) and transform constants 264..267. `RSX_ENGINE_SHADER_DUMP=<dir>` writes each
    pipeline's HLSL. Stencil reads `st <on> sf <func> ref <ref> m <func mask>/<write mask> op <fail>/<zfail>/<zpass>`.
    `passes.txt` also gets, in pass order, an `xfer read|wrote` line for every 2D transfer that syncs guest memory
    with an engine surface (source/destination surface, non-zero pixel count: shows where a copy-out of the scene
    falls and whether it carried anything) and a `drop (<reason>)` line for every guest draw the engine dropped
    (topology, vertex fetch, pipeline, no target). `RSX_ENGINE_COHERENCE_LOG=<n>` logs the first n syncs to stderr.
  * `VK_ENG_DEBUG_NODS=<mask>` turns pipeline tests off: 1 depth, 2 stencil, 4 culling, 8 colour mask. If an
    image appears with a test off, that test is what rejects the draws.
* **Who fills the FIFO, and when**: `SPU_DMA_EA` lines carry `t=<ms>`; `SPU_DMA_EA_DUMP=<n>` hex-dumps the whole
  payload of the first n lines that had a `SPU_DMA_EA_FIND` hit. `GCM_EXEC_WATCH=<io lo>-<io hi>` logs each method the
  walker executes from that FIFO range with its time, so a block can be placed before or after the job that wrote it.
  `RSX_ENGINE_FP_LOG=1` prints each distinct fragment program the FIFO selects (material blocks executed even with no
  draw after them); in a pass dump, `draws.txt` also lists `m 08E4/1808/1824/1814` in FIFO order (an FP select with no
  BEGIN_END after it is a material block without geometry). `GCM_MARKERS=<n>` prints the title's NOP debug markers
  (`0x68750001` + ASCII; GoW:A only emits `fxZPrePassEnd`). Note `PPU_WVAL` parses with base 0: pass `0x...` (a leading
  0 means octal).
* **Who reads a range / lift vs interpreter**: `SPU_DMA_EA_GET=1` adds GET-class transfers to `SPU_DMA_EA`, with an
  `[ea-get]` line carrying the 16 source bytes as they are in guest memory at that moment (a report slot's
  `{timer, value, zero}`). `SPU_NOLIFT_IMG=<lo>[-<hi>][,...]` (with `SPU_INTERP_UNLIFTED=1`) drops the lifted code of
  those SPU images so they run in the interpreter -- an A/B for one job module (GoW:A's EDGE geometry module is image
  225, `jobmod_d93503647dd8`; its streamed fragments are 300-339). `SPU_INTERP_UNLIFTED=1` also catches a
  branch into LS code that has no lift at all (after the self-modifying-code microstep fails) and interprets it,
  logging `interpreting unlifted LS <addr> (lr ...)`, instead of halting the SPU. The interpreter shares `spu_helpers.h`, so it does not
  rule out a helper bug (M0.36 was exactly that: host NaN semantics in the float helpers).
* **What a job reads, and which way it branches** (M0.36):
  * `SPU_DMA_EA_T0=<s>` keeps `SPU_DMA_EA` (and the knobs below) silent for s seconds; `SPU_DMA_EA_PC=<hex>` keeps
    only DMAs issued from that SPU pc (lifted pcs are function entries, not the exact `wrch`).
    `SPU_DMA_EA_GETDUMP=<n>` hex-dumps the source of the first n GETs.
  * `SPU_LIST_EA=<lo>-<hi>` logs GET-list (`GETL`) elements in the range with up to 0x120 source bytes
    (`[list-get]`, cap `SPU_LIST_EA_MAX`). List elements never reach `SPU_DMA_EA`, so without this a job looks like
    it reads only its descriptor.
  * `SPU_BRTRACE=<wid>` (interpreted code only, so pair it with `SPU_NOLIFT_IMG`) logs every taken branch
    `from>to` of the policy-mode SPU running that workload, with the condition register's preferred word, folding
    repeats into `x<n>`; cap `SPU_BRTRACE_MAX`. Split by job and diff a job that emits against one that does not.
  * `SPU_ITRACE=<lo>-<hi>` (same gate) logs each instruction in that LS range with its target register(s) after
    it ran (`[it]`); `tools/spu_disasm.py` annotates it.
  * `SPU_FORCE_BR=<pc>@<wlo>-<whi>` makes a conditional branch at LS pc always taken for those wids (A/B: GoW:A
    `5414@15-17` skips the EDGE pre-cull).
* **Occlusion A/B (walker)**: `GCM_ZPASS_VALUE=<hex>` (ZPASS count written, default FFFF), `GCM_REPORT_TIMER0=1` (zero
  timer), `GCM_REPORT_SKIP=<io lo>-<io hi>` (leave those main-memory reports pending). `SPURS_PM_DMA_WID=<n>@<s>` starts
  that DMA log s seconds in (its 40000-line cap otherwise runs out during boot).
* **Video (cellVdec)**: `VDEC_LOG=<n>` logs the first n pictures handed to the title (format, destination, centre
  luma: 16 is black), `VDEC_CALLS=<n>` the first n GetPicItem/GetPicture calls with their result; `Open` prints the
  title's callback OPD. `PS3RECOMP_FFMPEG=0` forces the black-picture path.
* **Log volume**: every log line from every thread goes through one stdio lock, so a chatty line slows the game
  (1.3 M lines in 70 s cost ~40% of the frame rate). The port defaults to `PS3_VERBOSE=0`; set `PS3_VERBOSE=1` for the
  `[WAIT]`/`[evt]` lines. Per-task lines print for the first tasks only; `SPURS_TASK_LOG=1` prints all of them.
  Measure performance with the defaults.
* **Interpreted job code**: `SPU_JOBMOD_LOG=1` prints each distinct (code base `LS[0x1320]`, module header) a
  policy module jumps into with no lifted module. `SPU_INTERP_MISS=1` ranks unresolved branch targets by interpreted
  time every 20 s (`wid img pc n ms`). A target inside a module's image is a missing entry: feed the log to
  `analysis/interp_miss_to_extra.py` and re-lift. A target past the image is code the module does not contain.
  The micro-stepper advances a few instructions before handing off, so only the entry pc is a valid target.
* **Where blobs land in local store**: `SPU_LSA_GETLOG=<lo>-<hi>` (hex LS) prints each distinct (lsa, ea, size)
  GET into that range with the workload and the first words (GoW:A: EDGE code fragments at LS `0x11090+`).
  `analysis/lift_edge_frags.py` also takes the previous `edgefrags_register.c` as an input and keeps its placements.
* **WWS job lists (GoW:A)**: `PPU_TRACE_ADDRS=464F1C PPU_TRACE_DUMP=464F1C,<list array EA>,100
  PPU_TRACE_DUMP2=464F1C,3,80,1` shows a BatchJobList's entries and its header rows at every batch wait. An entry is
  8 bytes, type in the first halfword: 0 end, 1 job, 2 sync, 3 nop, 4 jump to list. Anything else in a list is
  corruption; `SPU_DMA_EA=<lo>-<hi>` on it names the writer. `SPURS_PM_ARG_BE=1` (port default) passes the PM's
  64-bit workload argument big-endian (word 0 high, word 1 low).
* **Stuck DMA guard**: a job that re-issues the same transfer `SPU_DMA_REPEAT_LIMIT` times (default 256) for
  `SPU_DMA_REPEAT_SECS` of wall time (default 30) is halted with `[spu-dma] ... identical transfers`. Polling a
  value another agent writes (EDGE waits on the RSX label `0x70000C80` for ring space) is the same transfer too,
  so the count alone is not a hang.
* **Draws that vanish**: `[rsx engine] pipeline table full` / `drop (pipeline)` lines in `passes.txt` mean the
  backend could not create a pipeline; GoW:A's first level needs about 1300 (the table holds 8192).
* **SPURS kernel pacing**: `SPURS_PASS_SLEEP=<ms>` (default 1) is the pause between kernel passes;
  `SPURS_PASS_GREEDY=1` (GoW:A port default) skips it after a pass that found work; `SPURS_IDLE_MAX=<n>` caps the
  idle back-off (log2 passes, default 4).
  `SPURS_KERN_THREADS=<n>|spus` (GoW:A port default: `SPURS_KERN_SPUS=0,1,5`, i.e. 3 threads with audio on SPU 5) runs n host kernel threads per instance, with a per-wid
  claim; thread k plays SPU k and skips workloads whose priority on SPU k is 0 when another thread may run them,
  before it consumes their readyCount or signal bit (`priority per SPU` is printed at registration). `SPURS_KERN_SPUS=<a,b,...>` sets the SPU each thread plays (and the thread count); each thread
  visits workloads in its SPU's priority order. `j<n>` in `[pm-prof]` counts job-code entries per wid;
  `m<pct>%c<pct>%` is the share of the wid's time spent in SPU channel writes (MFC commands, i.e. DMA copies) and
  channel reads (tag/event waits), measured only under `SPURS_PM_PROF`. `[pm-prof]` shows `z<pct>%` per wid (share of its time spent in idle runs, polls == 0) and `idle=` overall;
  a high idle share means the kernel is busy polling, not working.
  `SPURS_PM_EXITSTAT=<n>` prints, every n module exits, the most common exit paths (wid, last two function
  entries, r3). Divide `j` by the run count in `[pm-prof]` for jobs per module run: GoW:A's wwsjob PM runs about
  one job per run, because the HLE `selectWorkload` poll answers r3 = 0 (= "switch to wid 0") and the PM compares
  the answer with its own wid (LS `0x37A4`). Two experiments to keep a module draining, both OFF by default
  because both stop the game (all lists idle, main thread waiting on RenJobSet or the SPU task batch):
  `SPURS_PM_POLL_SLICE_US=<us>` answers the module's own wid for <us> of its run, and `SPURS_PM_RERUN_US=<us>`
  re-runs a workload at once after a run that executed jobs. The kernel's per-dispatch readyCount decrement and
  the signal-bit clear are the first suspects.
* **A/B timing hygiene**: the game keeps a FIOS cache of archive data at
  `vfs/cache/BCUS98232/fios/cache.{idx,dat}` that survives between runs. A warm cache made the menu ~30 game
  frames/s against ~20 with a cold one, and a cache left by a run killed mid-write once made the next build loop
  reopening it. Delete both files before every timed run (the `work/ab_*.sh` scripts do).
* **Draw engine time by phase**: `ENG_PHASE_STATS=<s>` prints, every `<s>` seconds, the walker's time in draw
  methods and in BEGIN_END groups. It splits that by phase (fetch, pipeline, textures, index build, backend), then
  splits textures by path: depth/colour snapshots, depth/colour stored to guest memory, hash, re-upload, miss.
  `ENG_SLOW_MS=<ms>` logs single groups slower than that, with their sizes (first 64).
* **Slow Vulkan driver calls**: `VK_SLOW_MS=<ms>` logs every queue-lock wait, submit, fence wait, allocate, map,
  free and idle that takes longer, with the thread and the number of live `VkDeviceMemory` objects (up to 3000
  lines). This is how the M0.59e hang was found: seconds inside `vkFreeMemory`/`vkAllocateMemory`, outside any
  fence.
* **Where a Vulkan draw's time goes**: `VK_DRAW_PROF=<s>` prints, every `<s>` seconds, the average µs per engine
  draw in cache lookups, descriptor work, vertex/index copies, command recording and the batch flush, and splits the
  descriptor work into set allocation, uniform copies, view/sampler resolve and `vkUpdateDescriptorSets`. This found
  the constants being read back out of write-combined memory (M0.62).
* **Walker fast paths, off for A/B** (toolkit, all default on, each read once): `RSX_REMAP_FAST=0` dedups a draw's
  vertex references through the old hash table (every draw hashed, probing a table sized for the largest draw seen)
  instead of the direct table / per-draw table; `RSX_VP_HASH_CACHE=0` re-hashes the vertex program byte by byte for
  the pipeline key on every draw instead of remembering the hash by content; `RSX_GUEST_READ_FAST=0` makes a surface
  readback into guest memory count non-zero pixels for the pass dump again even when no dump runs, and convert rows
  in place before copying; `RSX_TEXHASH_LANES=0` uses the single-chain texture content hash. All four give the same
  frames; setting all four to 0 is the previous walker.
* **Profile one SPURS workload**: `PS3_SAMPLE_WIDS=<w>[,<w>...]` (with `PS3_SAMPLE` / `PS3_SAMPLE_HOST`) keeps
  only samples of SPURS kernel threads that are running one of those workloads at that moment. Workloads share kernel
  threads, so without it the audio workload (wid 8) and the EDGE ones (15-17) land in the same buckets.
* **Time inside a system DLL**: `PS3_SAMPLE_MOD=ntdll.dll` (one module) also buckets that module's samples by RVA
  (`[samp] modrva ntdll.dll+0x...`); resolve them against the DLL's export table, nearest export at or below. This is
  how the `longjmp` unwinding was found (M0.64). `SPU_LONGJMP_UNWIND=1` brings the unwinding jump back.
  Each such sample is also unwound to the first frame in the exe (`[samp] modcaller rva 0x...`, resolve against the
  link map): which code calls into the DLL. That found an uncached `getenv` in `spu_run_with_halt`.
* **SPU indirect-branch dispatch cache** (M0.79): `PS3_SPU_IBC=0` turns it off, to check whether a dispatch bug comes
  from the cache. It is also off whenever `SPU_IBCOV`, `SPU_IB_STATS`, `SPU_POLLTRACE`, `YDKJ_POLTRACE`,
  `SPU_JOBTRACE`, `SPU_JOBEXEC` or `LBP_HLE_JOBDONE` is set, and while job capture or replay runs, so those traces
  see every dispatch.
* **Is the RSX walker the limit?** (M0.80): `ENG_PHASE_STATS=30` prints, every 30 s, the walker's time in methods
  and in draw ENDs split by phase (fetch, pipeline, textures, index build, backend). `RSX_SLOW_PCT=<n>` busy-waits
  n% of every walker pass: if frames/s falls by about n/(100+n), the walker is binding. Per-thread host CPU:
  `RSX_PIPE_FAST=0` (M0.81) brings back the per-draw linear pipeline scan and fragment-program walks, to check whether
  a pipeline mix-up comes from the index or the memo. `VK_DRAW_PROF=<s>` splits the Vulkan draw into lookup,
  descriptors, copies, recording and flush per draw, and counts render passes; `VK_PASS_MERGE=1` (off) lets
  consecutive batched draws share one. `scripts/thrsnap.ps1` diffs `Get-Process` thread times between two moments of a run.
* **Sub-millisecond sleeps** (M0.80): the port sets `PS3_USLEEP_WAIT_US=500`, so a guest `usleep` of 500-999 us
  waits on a high-resolution timer instead of spinning; `0` brings the spin back for every sub-ms sleep.
* **Harness instrumentation**: `work/ab_exe.sh` sets `SPURS_PM_PROF=60` (and `GOW_MAIN_PROF`), which times every SPU
  channel read/write with two `QueryPerformanceCounter` calls, ~5% of EDGE time. A/B comparisons stay valid; absolute
  numbers are a bit pessimistic. `NOPMPROF=1` runs without them (no `[pm-prof]`, so no EDGE ms/job).
* **Indirect branches**: `SPU_IB_STATS=1` prints, every 20 s, how many indirect branches the SPU dispatcher resolved
  and the 24 most frequent by (workload, image or resident overlay, target, link register).
  `SPU_IB_STATS_WIDS=15,16,17` counts only those workloads.
* **SPU job capture**: `SPU_JOB_CAPTURE=<dir>` writes one file per policy-module job, `job_<wid>_<seq>.bin`: LS and
  GPRs at entry, then every GET/PUT (with the bytes), GETLLAR/PUTLLC/PUTLLUC line, channel read, JobApi dispatch,
  and LS and GPRs at the end. `SPU_JOB_CAPTURE_WIDS` (default `15,16,17`), `SPU_JOB_CAPTURE_N` (default 100, all
  threads), `SPU_JOB_CAPTURE_T0=<s>` (skip boot), `SPU_JOB_CAPTURE_EVERY=<k>` (keep one job in k: a
  sample spread over minutes and workloads instead of one burst). One `[jobcap]` line per job. `analysis/jobcap_dump.py <file> [-v]`
  prints a file. Format: `runtime/spu/spu_channels.c` (toolkit), see `docs/EDGE_NATIVE.md`. Captures hold game data:
  keep them under `work/`. Version 2 adds the context state block, interrupts taken and `rchcnt` values; replay
  refuses version 1.
* **SPU job replay**: `SPU_JOB_REPLAY=<dir|file>` makes the exe re-run those captures on the lifted code and exit,
  before the ELF loads (no PPU, RSX or SPURS threads; about 2 s for 200 jobs). GETs, atomics, channel reads and polls
  come from the transcript; PUTs and the final GPRs/LS are compared. One `[replay] job_... PASS|FAIL <ms>` line per
  job (a FAIL names the event index, recorded vs issued, pc and recent hops, or the first differing byte), then
  `[replay] N/M jobs identical`; exit code 0 only when all are. `SPU_JOB_REPLAY_REPEAT=<n>` runs each job n times and
  reports the median: the timing tool for native EDGE stages. Toolkit `runtime/spu/spu_job_replay.c`. The exe still
  takes the ELF path as its argument (it prints its usage and exits with 2 without it), though the ELF never loads.
* **Job lab build**: capture, replay and the job timer exist only in an exe configured with
  `-DPS3RECOMP_SPU_JOBLAB=ON` (toolkit CMake option, default off). Without it the hooks compile to nothing (their
  run-time checks cost 1.1% level fps with the env unset), `SPU_JOB_CAPTURE` is ignored and `SPU_JOB_REPLAY` says
  so and exits. Lab exes are staged as `build_runLab`; production builds stay off. Toggle with
  `cmake -S work/port -B work/port/build_v017 -DPS3RECOMP_SPU_JOBLAB=ON|OFF` (only runtime files rebuild).
* **Function boundary probe** (job lab): `analysis/spu_fnprobe_lift.py <lift.c> <out.c> <fn> [callees...]` wraps
  one lifted function in `spu_fn_probe` (toolkit `runtime/spu/spu_fn_probe.c`) and routes its and the listed
  callees' LS reads. Build it with `LAB_LIFT="<lift dir> <out.c>" sh work/buildlab.sh <dest>` (relative path for
  out.c; the production lift goes back before the production rebuild). Then `SPU_JOB_REPLAY=<dir>
  SPU_FN_PROBE=<fn>` prints `[fnp] <fn> <job> #<call> <us> us  r3..r10=...  r1=  wr=<ranges>  rd=<ranges>
  gpr=<changed>` per call in each job's first run; `SPU_FN_PROBE_SAVE=<dir>[,<n>]` saves entry/exit GPRs+LS per
  call (`fnp_*.bin`, game data).
* **Native EDGE loops** (`port/gow_edge_native.c`, docs/EDGE_NATIVE.md): the d935 lift is built through
  `port/gen_edge_native_lift.py`, which adds the switch. `GOW_EDGE_NATIVE`, read once: unset or 1 = native v3, 0 = lifted
  (`GOW_EDGE_V2=1`: v2), 2 = native v1, 3 = check v3/v2 and 4 = check v1 (run native on a copy, then lifted, and
  compare all GPRs and LS at loc EDE8: `[edge-check] N calls checked, M differ`, every 4096 calls and at exit, plus
  the fallback counts; any `[edge-check] call N: ... differ` line is a mismatch, the first four `call N: r34 ...`
  lines are only the layout). `GOW_EDGE_FAULT=1|2` flips one
  LS bit or one r5 bit after the native loop, to show that the gates catch it.
  73E0's region (`loc_79B0`..`81C0`, `port/gow_edge_native_73e0.c`) has its own switch `GOW_EDGE_73E0`: 0 =
  lifted, unset/1 = native v3 (`GOW_EDGE_73E0_V2=1`: v2), 2 = v1, 3/4 = check (`[edge-73e0-check] N calls checked, M
  differ`, every 4096 calls and at exit); `GOW_EDGE_73E0_FAULT=1|2` flips one LS bit or one r60 bit.
  D640's two loops (`loc_D92C` triangle culling in D640, `loc_D4DC` vertex transform in D250,
  `port/gow_edge_native_d640.c`) share `GOW_EDGE_D640`: 0 = lifted, unset/1 = native v3 (`GOW_EDGE_D640_V2=1`: v2),
  2 = v1, 3/4 = check (`[edge-d640-check] D92C: N calls checked, M differ` and the same for D4DC);
  `GOW_EDGE_D640_FAULT=1..4` flips an LS bit (1 D92C, 3 D4DC) or a register bit (2 r5 in D92C, 4 r24 in D4DC).
  A880's loop (`loc_A950`, `port/gow_edge_native_a880.c`) has `GOW_EDGE_A880`: 0 = lifted, unset/1 = native v3
  (`GOW_EDGE_A880_V2=1`: v2), 2 = v1, 3/4 = check (`[edge-a880-check] N calls checked, M differ`);
  `GOW_EDGE_A880_FAULT=<n>[,<k>]` flips one LS bit (1) or bit 0 of r73 (2) in native run k (default the first).
  F538's loop (`loc_F620`, `port/gow_edge_native_f538.c`) has `GOW_EDGE_F538`: 0 = lifted, unset/1 = native v3
  (`GOW_EDGE_F538_V2=1`: v2), 2 = v1, 3/4 = check (`[edge-f538-check] N calls checked, M differ`);
  `GOW_EDGE_F538_FAULT=<n>[,<k>]` flips one LS bit (1) or bit 0 of r20 (2) in native run k (default the first).
  6AC8's loop nest (outer `loc_6BE0`, inner `loc_6DE0`, `port/gow_edge_native_6ac8.c`) has `GOW_EDGE_6AC8`: 0 =
  lifted, unset/1 = native v3 (`GOW_EDGE_6AC8_V2=1`: v2), 2 = v1, 3/4 = check (`[edge-6ac8-check] N calls checked, M
  differ`); `GOW_EDGE_6AC8_FAULT=<n>[,<k>]` flips one LS bit (1) or bit 0 of r33 (2) in native run k.
  Overlay fragment `edgefrag_7228c0_11150`, function 11430's loop (`loc_11460`, `port/gow_edge_native_o11430.c`,
  hooked at build time by `port/gen_edge_frag_lift.py`) has `GOW_EDGE_O11430`: unset/0 = lifted, 1 = native v3
  (`GOW_EDGE_O11430_V2=1`: v2), 2 = v1, 3/4 = check (`[edge-o11430-check] N calls checked, M differ`);
  `GOW_EDGE_O11430_FAULT=<n>[,<k>]`: 1 the LSB of an output x (only check mode sees it), 3 x's mantissa bit 16,
  2 bit 0 of r9.
* **Job proper vs module run**: in a lab build, `SPURS_PM_PROF` adds `[pm-jobt] w<wid>: jobs= timed= proper=<us>/job
  p50<= p90<= module=<us>/job proper/module=<pct>%`: wall time from each job start to its return to the link (the
  entry bracket's drain return), against the whole module run per job. The rest is the policy module's own work:
  kernel entry and selection, job fetch and staging, completion, interrupts, exit. Read a row only where
  `timed` is about `jobs`: a job that ends by halt or by the next job start is never timed (GoW:A EDGE wids
  15-17 are fully timed; w12 and w14 are not).
* **Targets sampled through guest memory**: with `ENG_PHASE_STATS` set, `[eng-via-guest]` lists each distinct way a
  draw samples a depth or colour target as a texture by storing it to guest memory first (z2guest/s2guest): the
  texture format, the target's format and size (first 32).
* **SPU direct tails**: lifted known-target branches tail-call their target (SPU_DIRECT, toolkit default on).
  `PS3_SPU_DIRECT=0` sends every transfer back through the drain loop's trampoline; the drain-step diagnostics
  (`SPU_LOCKSTEP`, `SPU_TAILRET`, `SPU_PCHIST`, `SPU_STEPTRACE`, `SPU_JOBDRAIN`, `SPURS_PM_FLOW`, ...,
  `SPU_JOB_CAPTURE`, `SPU_JOB_REPLAY`) turn it off by themselves, so they see every step.
* **Depth read as A8R8G8B8 on the GPU**: `RSX_Z2ARGB=1` (GoW:A port default) builds a D24S8 depth target sampled as linear A8R8G8B8
  on the GPU instead of storing it to guest memory. It prints `[z2argb] 60s:` with conversions, textures created,
  failures and backend time. `RSX_Z2ARGB=2` also does the store and compares the texels (`[z2argb-check] 60s: N
  compared, M with differences`). Leave it off when something on the CPU, SPU or RSX reads those guest bytes
  (only the store keeps them current). `[eng-zsnap] refused:` says, once per reason, why a depth snapshot was
  not taken (e.g. `had_write 0`).
* **Who shares which lock line**: `SPU_ATOM_WIDS=<s>` lists, per SPURS workload, the 128-byte lines its atomics
  touch (lines with 100+ hits). `put` counts PUTLLC, PUTLLUC and PUTQLLUC together. `SPU_ATOM_EA=<ea>`
  (`SPU_ATOM_FULL=1`, `SPU_ATOM_PUTONLY=1`) then dumps one line's contents at each write.
* **LS probes and FP check in lifted SPU code**: the port builds its lifted SPU sources with `SPU_LS_PROBES=0` and
  `SPU_FP_CHECK_HOOK=0` (CMake `GOW_SPU_LS_PROBES`, default OFF). `SPU_LS_WATCH`, `SPU_SMC_WATCH` and
  `SPU_LS_LOWREAD` then see only accesses made by the runtime, and `SPU_FP_CHECK=1` checks nothing. Configure with
  `-DGOW_SPU_LS_PROBES=ON` to get both back. That costs ~10-15% per EDGE job (M0.61).
* **Host-side profile**: `PS3_SAMPLE=<ms>` samples every thread's RIP (reports every 10 s: guest functions,
  host modules, per-thread busy/ntdll split). `PS3_SAMPLE_HOST=1` buckets every sample by executable RVA and
  thread instead (lifted SPU job modules and overlay fragments are not in the guest map); resolve with
  `python repo/analysis/samp_resolve.py run.log build_vk/MyGameRecomp.map t<seconds> [top]` (the build links
  with `/MAP`). It prints the top symbols overall and per thread. `PS3_SAMPLE_RANKS=<n>` raises the 400 RVA buckets each report
  keeps (lifted code spreads over thousands). Since M0.61 the sampler keeps thread handles: before, one round took
  ~430 ms whatever the period.
  A top count above 25 also lengthens the per-thread lists. Write watches (`PPU_WDEC`, `PPU_WJUMPBACK`,
  `PPU_WVAL`, `PPU_WWATCH`) are read once; with none set, a guest store skips the watch walk entirely.
* **EDGE cost per job**: `python repo/analysis/pmprof_edge.py run.log...` reads the `SPURS_PM_PROF=60` lines and
  prints the median time per EDGE job (wids 15-17) over the windows where EDGE is busy. Two runs reach the level
  at different times and in different scenes, so frames/s alone is a noisy A/B; this is the steadier number.
* **Frame split (main thread)**: run with `PS3_POLLTOP=60 GOW_HEARTBEAT=30`, then
  `python repo/analysis/frame_wait.py run.log` prints, per 60 s window, the ms per game frame the main thread
  sleeps at `lr=0x003E8388` (the frame's wait for WWS job lists; the policy module `gowpm0` clears the flag at
  `0x0099AAC0` by DMA when the list completes) and the rest (its own work and other spins). Frames per 300 s
  varied +-12% between identical builds; compare these medians instead. Do not compare a run made with
  `PS3_SAMPLE` against one without: the sampler slows the threads it samples.
* **FIFO walker latency**: `GCM_PARK_SPIN_US=<us>` (port default 4000, 0 = off): when the walker stops on a
  self-JUMP park, the frame loop yield-polls that word instead of sleeping its 4 ms tick, so each EDGE segment
  patch is followed at once.
* **Stuck SPURS workloads** (port default `SPURS_PM_HANG=10`; `0` turns it off): `SPURS_PM_HANG=<s>` reports policy-module runs that have not returned after s seconds
  (every 2 s: wid, lane, pc, lr, r3..r5, event status/mask, interrupt enable, list-stall status, srr0, LS words at pc,
  then the tag mask, last tag status and every parked stall-and-notify list). Lifted SPU code updates pc only on
  cross-function branches, so pc names the last function entered, not the exact instruction. The port runs all of
  an instance's modules on one host thread: one stuck run stops every workload (`[spurs-pm] poll` lines stop).
  A `[SPU] more than N contexts hold an MFC engine` line means contexts share tag state: fix that first;
  `SPURS_WINFO_LOG=1` logs what `cellSpursGetWorkloadInfo` returns (contention/readyCount changes per wid).
* **SPU DMA lists / interrupts**: `SPU_LIST_TAG=<n>` traces list ISSUE (with the first two elements and LS word 0),
  stall ACKs and tag updates for tag n, and reports any DMA during a PM run that changes LS word 0 (the WWS
  interrupt vector), dumping the whole LS to `ls_vector_clobber.bin` on the first change. `[spu-int] ... is no
  branch -- not taken` means the vector was overwritten.
* **Lifter regression to remember**: callee-saved restore folding (`_cs_N`) must never apply to a mid-function
  spill/reload pair; if a function computes garbage right after a reload from the stack, check its lifted C for
  `ctx->gpr[N] = _cs_N` at that point.
* **SPU register hazards**: an instruction whose destination is also a source (`bisl $0,$0`) must read its
  operands first. The interpreter got this wrong for `bisl` (M0.20); suspect it whenever a job "returns" from an
  API call that never ran (no PM-side trace, result = whatever was in `r3` before).
* **Frame count**: `GCM_FLIPCOUNT=1` logs every flip with a timestamp.
* **Frozen frame clock / walker** (port): `GOW_CLOCK_WATCH=<s>` names the frame-clock step that made no progress for
  s seconds. While draining, it adds `[clock-watch] rsx_phase=` (1 clear, 2..5 BEGIN flush, 0x100+n
  `bind_texture(n)`, 6/7 draws) and `vk_fence_waiting` (GPU wait or CPU work). When flips stop, it prints a
  `[walk]` report: get/put/ref/ref queue, the last 16 decoded FIFO words, the words at get, and what the ring holds now
  after the last decoded word. `GCM_SEMALOG=1` logs every new ACQUIRE blocking point without the rest of
  `GCM_RECDBG` (which slows the walker enough to hide timing stalls).
* **FIFO park/kick protocol**: `GCM_PARKLOG=1` logs each self-JUMP park the walker reaches and when it is released,
  `[kick]` for every PPU store of "JUMP to the next word" (the title's park release), `[park-patch]` for SPU PUTs
  over the current park, and `[park-reset]` if an SPU PUT writes a self-JUMP back over a released park. The `[walk]`
  report then lists recent kicks and the `GET_REPORT`s within ±0x14000 of get (offset range
  `GCM_WALK_REPORT=<lo>-<hi>`, default 0x1E000-0x20000, the RsxSync report slots).
  The runtime hooks behind it (`g_spu_put_observer`, `g_ppu_jump_store_observer`) are NULL unless a library installs them.
* **Walker history and unfilled holes** (`[walk]` report, needs `GCM_PARKLOG=1`): `spu-put` lists the SPU PUTs into the
  FIFO ring near get (ring range `GCM_PUTRING=<lo>-<hi>`, IO, default 0x280000-0xA80000; wid 13 RenderJobs writes 0x4000
  chunks with self-JUMP holes, wid 15-17 EDGE fill them); `ref to park` scans `GCM_WALK_SCAN=<lo>-<hi>` (EA, default
  0x21600000-0x21C00000) for the parked word's EA/IO and the words pointing at it (EDGE job descriptor and job-list
  entry); `jump` and `report-done` are the last JUMPs taken and main-memory GET_REPORTs executed; `armed report` lists
  RsxSync report slots (`GCM_WALK_ARMED`, default 0x2001ED00-0x2001EE20) whose `+0xC` halfword is still set, plus every
  GET_REPORT for them in the ring (old laps stay in memory: check the `jump` path before calling one "executed");
  `GCM_WALK_DUMP=<ea>[@<off>...],<bytes>[;...]` dumps raw memory; each `@off` dereferences (`a = *(a + off)`), so
  `967438@0@24,10` dumps the third dependency counter of the PPU job record `*(0x967438)`.
* **Frame timeline knobs**: `GCM_REPLOG=1` logs every main-memory GET_REPORT the walker executes (`[report] io arg t`);
  `SPU_DMA_EA=<lo>-<hi>,<lo2>-<hi2>` takes a second range and `SPU_DMA_EA_MAX=<n>` (decimal) lifts the 300000-line cap;
  `GCM_WALK_FIND=<vlo>-<vhi>[@<ealo>-<eahi>]` lists words with values in a range at the `[walk]` report; the report
  also prints the newest frame header (`frame +0x200..+0x810`) and the current words at the last jump sources.
  Combining `SPU_DMA_EA` (EDGE lists + FIFO), `SPU_ATOM_EA=99AA80` and `GOW_MLAA_LOG=1` gives the per-frame order
  X → W → R1 → Y → EDGE fills → R2 (see FEASIBILITY M0.24).
* **Who writes what into the FIFO / a job list**: `PPU_WVAL=<value> PPU_WVAL_LO/HI` (exact word, guest stack on the
  first 3); `PPU_WJUMPBACK=<ea-io delta>[,<min>-<max>]` logs PPU stores of a JUMP that points `min..max` bytes behind
  itself (a buffer wrapping to its begin); `SPU_DMA_EA_FIND=<vlo>-<vhi>` lists payload words in a value range in each
  `[ea-dma]` line and `SPU_DMA_EA_PEEK=<ea>` appends the current guest word at ea. GoW:A frame segments: record table
  `*(0x81B9F8)` (16-byte `{key, value}`, count `0x967E60`), sorted then linked by `0x369184`; trace `0x36971C` with
  `PPU_TRACE_DUMP=36971c,0x967E68,0x240,1` to see each frame's segment list.
* **Who moves a write pointer back**: `PPU_WDEC=<ea>` logs every 4-byte PPU store to ea that lowers it (small steps
  with a guest stack) and any other-width store covering it. `stvx` is lifted as a raw memcpy and inline lifted stores
  can bypass the watch: then `GOW_GUARD=<ea> GOW_GUARD_RIP=auto` (page guard; guest stack on the first write from each
  distinct lifted-code instruction) names the writer. `PPU_DELAY_FN=<fn>,<ms>` sleeps on every entry to a guest
  function (timing experiments; needs the `PPU_TRACE_FUNCS=all` lift).
* **Who overwrites a word, from any source**: `GOW_GUARD=<ea> GOW_GUARD_QUIET=1 GOW_GUARD_MAX=<hex>` reports the
  write that takes the word past `<hex>` with the faulting rva, host thread and host stack (`[GUARD] OVER-MAX`, then
  `host#n rva=`), however it was written (lifted code, a runtime memcpy, an HLE, the RSX walker). QUIET keeps one line
  per distinct writer of the watched 32-byte line. Resolve the rvas with the link map
  (`build_v015/MyGameRecomp.map`, image base 0x140000000); llvm-symbolizer finds nothing (no debug info). This is
  how `nv3089_blit` was caught writing the frame counter (M0.45).
* **RSX writes into main memory**: an NV3089 blit whose VRAM rectangle leaves VRAM is skipped and logged
  (`[nv3089] blit leaves VRAM`); `GCM_BLIT_OOB=1` performs it as before (it wraps into main memory).
  `GCM_SETTLE_MS=<ms>` (default 2, 0 = off): how long the FIFO walker re-reads a JUMP/CALL word to unmapped IO
  before resyncing, for a park released while its segment is still being written.
* **The title's own error message as a breakpoint**: `TTY_BT="a|b"` (alternatives separated by `|`) dumps the
  registers, host chain and guest stack of the thread that printed a matching line (first 6 hits).
  `TTY_BT_DUMP=<ea>:<len>[,...]` adds guest memory to the first hit; `@<ea>:<n>:<len>` treats ea as n pointers and
  dumps len bytes at each. GoW:A's `invalid size` comes from its CopyLocalMemory `0x374A80` (rejects sizes
  >= 0x20000000), fed by the VRAM-defrag copy queue drained by `0x373690`: count `0x96E710`, 12-byte entries
  `{dst, src, size}` at `0x96E714`, pushed on the main thread (by `0x372B70`/`0x3717B4` from the pending-move list
  `0x96E58C`, count `0x96E70C`). In the corrupted runs only the size word is garbage.
* **Every non-title writer of a page**: `GOW_GUARD=<ea> GOW_GUARD_QUIET=1 GOW_GUARD_PAGE=1` prints one
  `[GUARD] PAGE-WRITER` line (host chain) per distinct writer of the whole page that is not lifted PPU code -- the
  runtime, an HLE, the RSX walker, an SPU DMA. The title's own stores are not reported.
* **Record arrays**: `GOW_GUARD_LEN=<hex bytes>` extends the guard from `GOW_GUARD` over several pages, and
  `GOW_GUARD_NEG=<stride>,<off>` reports the write that makes the 32-bit field at `<off>` of a record negative
  (`[GUARD] NEGATIVE`, with host stack, guest registers and guest stack). `PPU_DESC_CHECK=<fn>,<table>,<count>,<limit>`
  walks a pointer table of block descriptors `{+8 offset, +0xC size}` on every entry to fn and reports any that
  ends past limit (`[DESC] bad`). GoW:A: `GOW_GUARD=2154B4C0 GOW_GUARD_LEN=23400 GOW_GUARD_NEG=80,C`,
  `PPU_DESC_CHECK=373690,96B51C,96DDBC,0C034B80`. The release that writes the size is `0x370E48` (object in r4:
  `+0x1C` size, `+0x44` descriptor). `GCM_LOCAL_SIZE=<hex>` overrides the reported local size.
* **VRAM-cache release guard** (`port/gow_vram_guard.c`, inserted by `scripts/patch_lift.py`): skips a release of
  `0x370E48` whose descriptor has nothing resident (`desc+0x20 == 0`), marks its side entry not resident and drops it
  from the LRU; logs `[vram-guard]`. On by default, `GOW_RELEASE_GUARD=0` turns it off (a mitigation, see STATUS M0.46d).
* **Following a pointer field in a trace**: `PPU_TRACE_DUMP=<fn>,<reg>+<off>,<len>,<hops>` starts at
  `gpr[reg]+off` before the hops, e.g. `370E48,4+0x44,30,1` dumps the descriptor the released object points at.
* **Two runs at once**: `INST=b` in `work/ab_exe.sh` gives the run its own `/dev_hdd1` (`vfs/cache_b`) and save
  root (`gamedata_b`); copy the build to `work/port/build_runA` / `build_runB` and run with `EXEDIR=runA|runB`
  so a rebuild never touches a running exe.
  A `REFRESH` file in a run dir naming a build dir (e.g. `build_v017`) makes the next `ab_exe.sh` run copy that
  exe and map in first. Never edit `ab_exe.sh` while runs use it: `sh` reads the script as it goes, and a
  changed file makes the running copy execute garbage (it truncated two logs once). Two game instances use
  ~2 GB each; a full PPU rebuild beside them needs `ninja -j6` (`NINJA_JOBS`), not the default -j14.
* **SPU PUT ordering** (`SPU_PUT_HEAD_LAST`, port default 1 since M0.49): the first 16 bytes of every SPU PUT are
  stored last, behind a store fence. An EDGE job releases an RSX FIFO park with the PUT that carries the segment;
  a front-to-back copy let the walker follow the release into unwritten words (`[cellGcmSys] FIFO JUMP to unmapped
  IO ... drain stops here`), and the EDGE job waiting for ring space (pc 0xC834, r5 = RSX label 0x70000C80) froze
  the game. `0` restores the plain copy.
* **Stuck policy-module runs**: `[pm-hang]` lines (every 2 s past the threshold) now name the loaded image, task and
  overlay and dump r0-r15 once per stuck run. `SPURS_POLL_SELECT=1` answers selectWorkload polls with a selection
  (own wid while productive); it deadlocks GoW:A on the 2-thread kernel, so it is only for scheduler experiments.
  `[vram-guard-underflow]` counts releases the VRAM guard refused because the block size would go negative.
  The VRAM descriptor table sits at 0x2154B4C0 or 0x8154B4C0 depending on the run: `TTY_BT_DUMP`'s `@` pointer
  form skips targets at or above 0x40000000, so dump 0x8154B4C0 in explicit `<ea>:2000` chunks. `[pm-hang]` also
  prints the run's poll count, last answer, live readyCount/contention and a loop-pc histogram (2000 samples).
* **Level frame rate**: `python repo/analysis/level_rate.py run.log...` prints three numbers:
  - `window`: game frames per second over 540 s, starting 60 s after the level load burst.
  - `play`: from the end of the loading screen to the end of the run.
  - `tail`: 600-1080 s.
  The loading screen counts ~17 game frames/s and can outlast the 60 s, so `window` sometimes reads high. Use
  `tail` (or `play`) to compare A/B pairs. Level entry time varies by run (120-330 s on the fast route).
* **SPU float**: the port builds with `SPU_FP_NATIVE` (CMake `GOW_SPU_FP_NATIVE`, default on): single-precision
  SPU ops run as native instructions under round-toward-zero. `SPU_FP_CHECK=1` compares every one with the exact
  path and prints `[spu-fp]` mismatches and a running count. Rebuild with `-DGOW_SPU_FP_NATIVE=OFF` to rule it out.
* **Async Vulkan submission**: `VK_ASYNC=1` (port default) keeps up to 3 batches in flight (`VK_ASYNC=0` waits at
  every flush, the thing to try for a rendering bug that might be ordering).
* **SPURS scheduling knobs**: `SPURS_PM_RERUN_US` (port default 2000; the second argument of `work/ab_exe.sh`
  overrides it), `SPURS_KERN_SPUS` (port default `0,1,2,3,4,5`), `SPURS_EDGE_DIAG=<s>` with `SPURS_EDGE_WIDS`
  (whether those workloads ever run side by side), `SPURS_MAXCONT="<wid>=<n>,..."` (lanes per workload, experiment),
  `SPURS_LANES="<wid>[,<wid>...]=<n>[;...]"` (up to n kernel threads run that workload's module at once, each as
  the SPU it plays, only on SPUs the workload has a priority on; the port's default is `SPURS_LANES=15,16,17=3`, `SPURS_LANES=off` disables it),
  `SPURS_RERUN_YIELD=1` (end a rerun window when a workload this SPU ranks higher is ready; logs `[spurs-ry]`
  counts every 60 s; default off, measured neutral on GoW:A),
  `SPURS_PM_RERUN_WIDS="<wid>[,<wid>...]=<us>[;...]"` (a rerun window of their own for those workloads; default
  unset, measured neutral on GoW:A). `PS3_SAMPLE_WIDS=<w>[,<w>...]` keeps only the host samples of kernel
  threads running those workloads (with `PS3_SAMPLE=2 PS3_SAMPLE_HOST=1`).
* **Timer resolution**: the port opts out of Windows 11 power throttling so its 1 ms timer resolution holds without a
  visible window (`GOW_TIMER_FIX=0` skips that). PS3_FTRACE shows the SPURS kernel threads' between-pass pauses as
  SPU_WAIT spans with `a=0x51EE` and resident-slot waits with `a=0x5107`. `GOW_CLOCK_WATCH=<s>` names the walker's
  state when flips stop.
* **Decrementer clock**: `SPU_HOST_NS=qpc` reads the SPU decrementer's host clock from QPC instead of the
  calibrated TSC (A/B switch; measured drift 0.75 ppm).
* **Faster runs**: `work/ab_exe.sh` reads the pad script from `NAV_PAD` (default `work/nav_pad_fast.txt`: CROSS
  from 40 s skips the intro movie, then Continue Game) -- the level is reached at ~160 s instead of ~420 s.
  `work/nav_pad_slow.txt` is the old schedule. `PPU_TRACE_ADDRS`/`PPU_TRACE_MAX` pass through to the run.
* **HLE that writes into the title's command context**: `_cellGcmSetFlipCommand(ctx, id)` writes a flip marker at
  `ctx->current`; its ring-wrap shortcut only applies to SDK-style contexts. Suspect HLE first when a title's
  `current` jumps without a store from title code.
* **WWS job-list state**: workload data `+0x08` = current row/list, rows at `+0x40+0x10*n` = {u16 high-water, u16 next
  index, u16 lanes...}; list at `+0x3C` (EDGE) or the four list pointers at `+0x30` (RsxSync). Entries `{u32 cmd, u32 arg}`:
  1 job, 3 nop, 4 end/next list, 2 and 5 are both gated at allocation on the halfword at `arg` being 0 (the PM reads the
  counter `(count<<16)|(wid<<8)|flags`). `SPURS_PM_HANG_LS=<ls>,<bytes>[;...]` adds LS dumps to `[pm-hang]`;
  `SPURS_PM_ENDLS_WID=<lo>-<hi>` prints them (`[pm-end]`) at the end of the next three runs of those wids once the
  `[walk]` report has fired. The WWS PM caches the list line at LS 0xD00 (EA at 0x1500).
* **Where is the main thread when flips stop?** `GOW_CLOCK_WATCH=<s>` prints three `[GSTACK:stall-main]` guest stacks
  (`ppu_dump_thread_stack(1, ...)`); identical sp/lr = blocked. This beat a day of reasoning about pipeline cycles.
* **SPURS tasksets**: `SPU_TS_COMMITLOG=<taskset ea>` logs the bitsets at each SPU commit to that taskset's header line
  and the candidates of the start scan (`[taskset-kick]`). `[taskset] ... not dispatched twice` marks a duplicate
  dispatch that was suppressed. A task ELF field with low bits set (e.g. `0x00681884`) was written by SPU-side
  `cellSpursCreateTask` (flags; the ELF is at `& ~0xF`).
* **SPU atomics on one line**: `SPU_ATOM_EA=<ea>[,<ea>...] SPU_ATOM_EA_MAX=0 SPU_ATOM_FULL=1` (up to 4 lines;
  `SPU_ATOM_FULL=2` prints the whole line, `SPU_ATOM_PUTONLY=1` skips GETLLAR polls). PUTLLC lines carry RAM and
  STORE, so a diff shows which word each job changed (used to prove a decrement was never issued rather than lost).
  WWS list row `+0x70` = `{u16 next index, u16 index}` plus one in-flight job slot per SPU lane (`FFFF` = idle),
  written only by the policy module (GoW:A PM: `pc 0x2540` allocates, `pc 0x35E0` completes).
* **Port defaults that hide GoW:A-specific walker traps**: `GCM_FIFO_CIRCLE_PASSES=0` (no "circling" resync to the
  fixed `put`), `VK_TEX_OFF=1` (no CPU texture decode until guest shaders exist). Override them on the command line.
* **`lr` in `[wwsjob]` lines** is `r0` at the moment of entry, not a link: a job resumed by `bi` after an API
  call shows whatever it had loaded into `r0` (e.g. the API table word).
* **MLAA HLE log**: `GOW_MLAA_LOG=1` logs every task share (label, counter before/after), not only the first 60.
* **MLAA**: `GOW_MLAA=lifted` runs the real MLAA tasks (image 14) instead of the HLE in `port/gow_mlaa_hle.c`.
* **SPURS tasks**: `SPURS_TASK2_STUB=1` restores the old accept-only `cellSpursCreateTask2`. SPU `BRANCH-TO-0` lines now
  print `ls[pc-4]`/`ls[pc]`/`r3`: a `stopd` (`0x280xxxxx`) or `stop` word there is a job assert, not a missing lift.
* **PSARC helpers**: `analysis/psarc_find.py <regex> <psarc...>` lists entries; `analysis/psarc_extract.py <psarc> <name> <out>`
  extracts one (use `MSYS_NO_PATHCONV=1` in Git Bash so `/DIR/NAME` is not rewritten).
* **Name hash**: Jenkins one-at-a-time over the upper-cased string, seed 0, no final mix (`0x3C4380`); DC symbol tables are
  sorted (hash, offset) pairs at the end of each `.dcb`.
* **Oracle**: RPCS3 boots the game and is good for behavioural comparison. It uses LLE for most firmware libraries,
  so it does not trace HLE calls.

## RAG index (local, optional)

The project is indexed in a local RAG server (collections `gow-recomp` for this repo and `ps3recomp` for the SDK).
`scripts/rag-manifest.json` declares what is ingested; `scripts/rag_hook.py` is a Claude Code `PostToolUse` hook
(registered in the parent folder's `.claude/settings.json`) that stages patches/JSON as `.txt` and asks the assistant to
re-ingest after each `git commit`/`git push`. Never add game data, firmware or lifted output to the manifest.
