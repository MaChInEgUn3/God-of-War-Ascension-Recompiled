# Debugging playbook

Lessons that cost days; keep them.

* **Game asserts are `twu r1,r1` traps and the lifter ignores them.** The first red line in the TTY is the first real
  assertion; everything after it is garbage state. Look for the *first* anomaly.
* **stdout and stderr interleave unpredictably** (HLE `printf` vs TTY). Never infer ordering from a merged log.
* **Function-entry tracer**: `PPU_TRACE_FUNCS=375754,3754a8 python ppu_lifter.py ...` (workers inherit the env), then
  run with `PPU_TRACE_MAX` and `PPU_TRACE_DUMP=<fn>,<reg>,<len>`. Re-lifting costs about 35 s, a full rebuild about 7 min.
* **Static analysis**: `analysis/ppudis.py <from> <to>` (capstone) and `analysis/findstore.py <ea>` (finds `lis`+displacement
  reads and writes of a global). capstone prints `add/subf/addi` as `addc/subfc/addic`; the lifter works from encodings.
* **Host-stack offsets in `[GCS:...]` call chains are not guest offsets**; they only name the functions involved.
* **Memory watch**: `GOW_GUARD=<hex ea>` arms the page guard (reports the writer function and chain).
* **GCM**: `GCM_MEMTABLE=970F50` dumps the title's mapping table and allocator list at each map/unmap;
  `GCM_UNMAP_STACK=1`, `GCM_UNMAP_SCAN=<hex,hex>`, `GCM_UNMAP_DUMP=1`.
* **SPU spins**: `SPU_SPIN_N=<n>` lowers the GETLLAR spin report threshold (default 4M) and dumps the code around pc + r0..r15; `RSX_SEMLOG=1` logs RSX semaphore/label methods.
* **SPURS**: `SPURS_PM_PIPE=1` (WWS pipeline counters; idle runs only first 8 per wid, active runs up to 3000), `SPURS_KEEP_READY=1` (kernel does not consume readyCount), `SPURS_WID_STATS=1` (runs per workload), `SPU_IRQTRACE=1` (SPU interrupts; nested takes dump the runtime-built shim), `SPURS_ZLOG=1` (every Edge Zlib inflate element, raw words), `SPURS_TRACE=1`, `PS3_SCTRACE=1`, `GOW_LLE_DUMP=<s>` (live CellSpurs), `GOW_LLE_WATCH=1`, `PS3_HLE_ARGS=<nid>`.
* **Editing gotcha**: in tool-driven Python heredocs, an escaped newline inside a C string can turn into a real
  newline. Compile after every such edit.
* **Run-time selectable tracer**: lift once with `PPU_TRACE_FUNCS=all`, then pick functions per run with
  `PPU_TRACE_ADDRS=3c93e8,2a0000-2b0000` (hex, ranges allowed) and cap with `PPU_TRACE_MAX`. Each line shows
  `tid`, `r3..r7` and `lr`. `PPU_TRACE_DUMP=<fn>,<reg|0xEA>,<len>[,<hops>]` also dumps memory (a `reg` of 32 or more is an
  absolute guest address; `hops` follows pointers first). The `all` build is slower; relift without it for normal runs.
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
    pipeline's HLSL.
  * `VK_ENG_DEBUG_NODS=<mask>` turns pipeline tests off: 1 depth, 2 stencil, 4 culling, 8 colour mask. If an
    image appears with a test off, that test is what rejects the draws.
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
* **SPURS kernel pacing**: `SPURS_PASS_SLEEP=<ms>` (default 1) is the pause between kernel passes;
  `SPURS_PASS_GREEDY=1` (GoW:A port default) skips it after a pass that found work; `SPURS_IDLE_MAX=<n>` caps the
  idle back-off (log2 passes, default 4).
  `SPURS_KERN_THREADS=<n>|spus` (GoW:A port default 2) runs n host kernel threads per instance, with a per-wid
  claim; thread k plays SPU k and skips workloads whose priority on SPU k is 0 (`priority per SPU` is printed at
  registration). `SPURS_KERN_SPUS=<a,b,...>` sets the SPU each thread plays (and the thread count); each thread
  visits workloads in its SPU's priority order. `j<n>` in `[pm-prof]` counts job-code entries per wid;
  `m<pct>%c<pct>%` is the share of the wid's time spent in SPU channel writes (MFC commands, i.e. DMA copies) and
  channel reads (tag/event waits), measured only under `SPURS_PM_PROF`. `[pm-prof]` shows `z<pct>%` per wid (share of its time spent in idle runs, polls == 0) and `idle=` overall;
  a high idle share means the kernel is busy polling, not working.
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
