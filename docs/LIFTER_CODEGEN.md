# Lifted SPU code: plan for tighter codegen

Status: **piloted 2026-11-08 and not recommended in this form.**
- Phase 1, applied by script to one job module and replayed, came out flat. Numbers are under "Pilot result".
- The cost is register count, not aliasing. Hot lifted functions touch a median of 70 SPU registers against
  16 host vector registers, so values go to the stack whether they live in `ctx->gpr` or in locals.
- The plan below is kept for the record. Nothing from it is in the build.

## Why

- The SPU side sets the frame's pace. At the start of a frame 4.9 of 6 SPUs are busy and the walker waits on
  EDGE output (x402). Every wait-side lever came out flat (x397-x401, x405-x406).
- Runtime helpers are ~18% of SPU kernel-thread time. Lifted and native job code is ~74% (x407).
- Generic runtime changes have little left to take. The indirect-branch cache at 8x size bought under 1% of a
  module run (x409).
- What is left is the code the lifter emits.

## What the code looks like now (measured, no run)

| Code measured | Host instr / SPU op | Stack share | Calls / 100 ops |
|---|---|---|---|
| Hottest wid 8 job function (`jobmod_5bc2fdee17f4_e11a08_spu_func_00015400`, 356 SPU ops) | 10.1 | 0.33 | 13 |
| 2718 functions with >= 50 SPU ops, op-weighted | 7.7 | 0.18 | 10.7 |
| Median of those 2718 functions | 7.2 (p10 4.3, p90 14.3) | 0.08 (p90 0.30) | |
| All 27761 lifted functions in 613 modules, op-weighted | 9.0 | 0.13 | 13.5 |

How these were counted:
- The objects are the production build, clang-cl /O2, disassembled with `llvm-objdump -d`.
- A SPU op is one emitted statement in the lifted C. That covers register writes, LS writes, branches, calls
  and channel ops.
- Stack share is the instructions that address `%rsp` or `%rbp`.
- The hottest function is in the upper part of the distribution, not an outlier. So the issue is general.

Why the compiler cannot do better:
- The lifter writes every result to `ctx->gpr[n]` (`tools/spu_lifter.py`, `g(i)` in `_translate`).
- The local store sits inside the same context object, `ctx->ls_store`, reached through `uint8_t* ctx->ls`
  (`runtime/spu/spu_context.h`, the `gpr[128]` and `ls` fields).
- An LS store goes through a byte pointer or a `may_alias` vector store. Either way it may alias any register
  and `ctx->ls` itself.
- So after each LS store the compiler reloads registers it already had, and reloads `ctx->ls` itself. 128-bit
  values it cannot keep in xmm registers are spilled.

## The change

**Phase 1: registers in locals per function.** The lifter declares a local `u128` for each SPU register the
function reads or writes. It loads the ones that are live at entry and works on the locals. It writes them
back to `ctx->gpr` only where something outside the function can see the register file. Those flush points
are:

- Every exit: `return`, `SPU_RET`, the `SPU_DIRECT` musttail calls and the trampoline path.
- Every call to another lifted function (`brsl`/`bisl` nested calls, `spu_drain_call`). After the call, the
  locals the callee may have written are reloaded.
- Every helper that reads or writes `ctx->gpr`, or that can leave by `longjmp`:
  - channel reads and writes, `stop`, `spu_halt` (`runtime/spu/spu_channels.c`, `s_spu_halt_env`);
  - the indirect-branch paths;
  - anything the native WWS driver or replay inspects at a stop.
- Trace mode (`spu_trace_rt`): flush per op, or keep trace builds on the current emitter.

Pure value helpers need no flush: the inline SIMD ops (`spu_a`, `spu_shufb`, ...) and `spu_ls_read128` /
`spu_ls_write128`. With the registers in non-address-taken locals, an LS store can no longer alias them.

**Phase 2: liveness.** This narrows the flush and reload sets at calls. Phase 1 is conservative and moves every
local the function touches. The hottest function has 47 calls, so this matters.
- Start with a per-function "written since the last flush" set.
- Then use the SPU ABI's callee-saved range only if replay shows the lifted code respects it. It is guest
  code, so do not assume.

**Phase 3, optional:** hoist `ctx->ls` into a local at entry. Phase 1 already removes most of its reloads.

Nothing changes for the runtime, the native EDGE stages or the WWS driver: they keep reading `ctx->gpr` at
flush points.

## Pilot result (2026-11-08, measurement only)

`analysis/lifter_locals_pilot.py` applies phase 1 mechanically to the named lifted functions:
- a `u128` local for every register the function touches, loaded at entry;
- the written registers stored back before every exit, stop, channel op and nested call;
- everything reloaded after a nested call.

The output was compiled with the production flags. A lab exe was built from it (`work/buildlab.sh` with
`LAB_LIFT`). The pilot never enters a production build.

**Static count** (hottest wid 8 function, 356 SPU ops, 83 registers; same compile, `llvm-objdump`):

| | Host instructions | Stack refs | Context-pointer refs |
|---|---|---|---|
| Lifted as now | 3820 | 1193 | 569 |
| Registers in locals | 3135 | 1316 | 176 |

The context round trips went away. The stack traffic rose, because 83 128-bit values do not fit in 16
registers.

**Replay timing** (General6 module a67e, functions 4930 and 4AB0: 74 and 128 registers, 5 nested calls).
- Setup: `work/jobcapA67`, lifted path (`GOW_A67_NOISE=0`), `SPU_JOB_REPLAY_REPEAT=20`, run order base, pilot,
  pilot, base.
- The pilot code is in its exe: per the map, 4930 grew from 3376 to 3968 bytes and 4AB0 from 58032 to 69472.

| Exe | Jobs identical | Median of per-job medians | Sum of per-job minimums |
|---|---|---|---|
| base (`build_runLab`) | 300/300 | 1.575 / 1.525 ms | 460.9 / 480.3 ms |
| pilot (`build_runLabP`) | 300/300 | 1.570 / 1.571 ms | 474.0 / 470.0 ms |

- The two pilot runs sit inside the 4% spread between the two base runs.
- The noise path is ~55% of this job: the same capture with the native noise takes 0.70 ms a job. A 20% gain on
  that code would have moved the sum by ~50 ms, well outside the spread.
- So the null holds: removing the aliasing reloads does not buy time here.

**Registers per function** (the 2721 lifted functions of 50 SPU ops or more; distinct `gpr[N]` a function names):

| | Value |
|---|---|
| Median | 70 |
| Quartiles | 50 / 80 |
| Share of SPU ops in functions with <= 20 registers | 0.4% |
| Share of SPU ops in functions with <= 30 registers | 2.1% |

Distinct registers are an upper bound on how many are live at once. The replay is the direct evidence that the
pressure is real in hot code.

Phase 2 (liveness-narrowed boundaries) only trims the load and store at the edges. It cannot help the body,
which is where the spills are. What would help is a different mapping, not this one. One option is wider
blocks of fewer live registers, which is register allocation across SPU code. That is a research project, not a
few days of emitter work, and is not planned.

## How it is checked

All on the job-lab replay harness. It runs captured jobs on lifted code with no PPU, RSX or SPURS threads and
compares every PUT and the final GPRs and LS (DEBUGGING.md, "SPU job replay").

**Baseline on the current tree** (2026-11-08, lab exe `build_runLab` built from production, `work/x58_run.sh`):

| Capture | Jobs | Result | Single run, median / job | `SPU_JOB_REPLAY_REPEAT=20`, median / job | Sum of per-job minimums |
|---|---|---|---|---|---|
| `work/jobcap4` (EDGE wid 15; most stages native) | 400 | 400/400 identical | 0.072 ms | 0.054 ms | 27.0 ms |
| `work/jobcapA67` (wid 3, General6; lifted job code) | 300 | 300/300 identical | 0.820 ms | 0.699 ms | 210.4 ms |

Steps:

1. **Correctness.** Every capture replays identical, PUT for PUT and byte for byte, on the new emitter's lift.
   Also add a fresh capture of wid 8 (`SPU_JOB_CAPTURE_WIDS=8`). It holds the hottest function and has no
   capture yet.
2. **Speed.** Sum of per-job minimums and the REPEAT medians, old lift against new, same machine state.
   jobcapA67 is the main yardstick because its job code is lifted.
3. **In game.**
   - First, check mode and a soak: no CHECK FAILED, no stall, frames spot-checked.
   - Then exe-to-exe pairs in ABBA order, reading the policy-module `us_run=` and game frames/s.
   - Report quiet and loaded columns.
4. **Code size and build time.** Record both. Locals per function should shrink the objects.

## Risks

- A missed flush point gives a wrong register at a stop or a call. Replay catches what the captures cover;
  check mode and the soak cover the rest.
- `longjmp` out of a helper with stale `ctx->gpr`. Every helper that can halt is a flush point by rule, not by
  inspection.
- Interrupts: `SPU_DIRECT` checks `int_enable`/`event_status` before the tail call. The flush has to come before
  that check.
- Huge functions (thousands of ops) may make the compiler spill the locals anyway. Phase 2 and the
  stack-share figure per function show whether that happens.

## Effort and order

| Step | Size |
|---|---|
| Phase 1 in the emitter, behind a lifter flag so both outputs can be produced | 1-2 days |
| A replay pass on all captures plus a wid 8 capture | half a day |
| In-game check mode, soak and A/B | 1 day |
| Phase 2 | after phase 1 numbers |

The change is generic, so it is an upstream candidate once replay and in-game numbers exist.

What it buys is not known until the replay numbers are in. Fewer host instructions per op in job code (~74% of
SPU time) only becomes frame rate where the SPU side binds, as it does now.
