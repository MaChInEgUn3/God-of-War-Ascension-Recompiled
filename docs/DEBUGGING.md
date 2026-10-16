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
* **SPURS**: `SPURS_TRACE=1`, `PS3_SCTRACE=1`, `GOW_LLE_DUMP=<s>` (live CellSpurs), `GOW_LLE_WATCH=1`, `PS3_HLE_ARGS=<nid>`.
* **Editing gotcha**: in tool-driven Python heredocs, an escaped newline inside a C string can turn into a real
  newline. Compile after every such edit.
* **Run-time selectable tracer**: lift once with `PPU_TRACE_FUNCS=all`, then pick functions per run with
  `PPU_TRACE_ADDRS=3c93e8,2a0000-2b0000` (hex, ranges allowed) and cap with `PPU_TRACE_MAX`. Each line shows
  `tid`, `r3..r7` and `lr`. `PPU_TRACE_DUMP=<fn>,<reg|0xEA>,<len>[,<hops>]` also dumps memory (a `reg` of 32 or more is an
  absolute guest address; `hops` follows pointers first). The `all` build is slower; relift without it for normal runs.
* **Guest memory probes** (port): `GOW_PEEK=<ea>,<words>` logs every change (20 ms poll); `GOW_SCAN=<hex bytes>` logs where a
  byte pattern appears. Both are host threads; times are ms since start.
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
