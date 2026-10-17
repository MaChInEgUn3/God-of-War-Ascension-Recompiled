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
* **Stuck SPURS workloads**: `SPURS_PM_HANG=<s>` reports policy-module runs that have not returned after s seconds
  (every 2 s: wid, lane, pc, lr, r3..r5, event status/mask, interrupt enable, list-stall status, srr0, LS words at pc);
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
* **Where is the main thread when flips stop?** `GOW_CLOCK_WATCH=<s>` prints three `[GSTACK:stall-main]` guest stacks
  (`ppu_dump_thread_stack(1, ...)`); identical sp/lr = blocked. This beat a day of reasoning about pipeline cycles.
* **SPURS tasksets**: `SPU_TS_COMMITLOG=<taskset ea>` logs the bitsets at each SPU commit to that taskset's header line
  and the candidates of the start scan (`[taskset-kick]`). `[taskset] ... not dispatched twice` marks a duplicate
  dispatch that was suppressed. A task ELF field with low bits set (e.g. `0x00681884`) was written by SPU-side
  `cellSpursCreateTask` (flags; the ELF is at `& ~0xF`).
* **SPU atomics on one line**: `SPU_ATOM_EA=<ea> SPU_ATOM_EA_MAX=0 SPU_ATOM_FULL=1`. PUTLLC lines carry RAM and
  STORE, so a diff shows which word each job changed (used to prove a decrement was never issued rather than lost).
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
