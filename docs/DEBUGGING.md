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
