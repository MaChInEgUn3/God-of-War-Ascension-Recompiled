# EDGE jobs as native code: capture, replay, replace

Status: design, started 2026-10-24. Progress log at the end.

## Why

- The level frame is bound by the serial chain of EDGE geometry jobs: SPURS workloads 15-17, job module `d935`, overlay
  image 225. On runZg, the main thread waits 168 ms per frame on EDGE, against a 283 ms median frame.
- One EDGE job costs ~0.21 ms. In an EDGE-only profile, 79% of that time is lifted SPU code, and five functions
  (`E550`, `73E0`, `A880`, `D640`, `F538`) make up ~38% of all EDGE samples.
- Inside those functions, 11-24% of samples are context-memory accesses and 11-16% are stack spills. Translated SPU
  code keeps the 128 SPU registers in memory and reaches local store through a pointer, plus the dispatch around it.
  Native host code for the same stages avoids all of that and can use 8-wide SIMD.
- Ceiling: the RSX walker drains for ~163 ms per frame. Native EDGE alone cannot take the frame below that: about
  1.7x game frames, ~12 presents/s. After that, the walker's known levers come next: the via-guest readbacks
  (catalogued in STATUS M0.62).

## Rules

- **Exact output.**
  - Integer stages (index/vertex decompression, output packing) must be bit-exact.
  - Float stages must also be bit-exact: reproduce the lifted operation order under the same MXCSR (round toward zero,
    FTZ/DAZ as the lifted code runs).
  - Culling must match the original decisions.
  - The replay diff is the judge, and any difference is a bug.
- **Clean-room.** Build from our own lifted code, our own captures and public talks about Edge Geometry. Never use
  leaked SDK sources.
- **One stage at a time.** Each native stage sits behind an env switch back to the lifted code, starting with the single
  hottest stage.
- **Captures hold game data.** They live under `work/` and are never committed.

## Capture (`SPU_JOB_CAPTURE=<dir>`, toolkit runtime)

- **Scope:** policy-module runs of chosen workloads (`SPU_JOB_CAPTURE_WIDS`, default `15,16,17`), N jobs
  (`SPU_JOB_CAPTURE_N`) after `SPU_JOB_CAPTURE_T0` seconds.
- **Job start:** where `g_spu_job_entries++` fires in `spu_indirect_branch` (a policy context, pc >= 0x4000, link in PM
  code). Snapshot:
  - the full 256 KB LS (it includes the policy module, so JobApi calls replay naturally);
  - all 128 GPRs and pc;
  - the context fields that dispatch reads (`image_id`, `resident_ovl`, `resident_code`, `policy_mode`, `spu_id`).
- **During the job, in order:**
  - MFC GET: cmd, ea, lsa, size and the bytes at transfer time;
  - MFC PUT: ea, lsa, size and the bytes;
  - GETLLAR: the line contents;
  - PUTLLC/PUTLLUC: the line written and the result;
  - every channel read and its value (decrementer, tag status, events): the nondeterminism sources.
- **Job end:** the job's return to the link it was entered with (the entry bracket's `spu_drain_call` returning there,
  or a dispatch to it), `spu_halt` on the context, or the next job start on the same thread. Other dispatches into PM
  code (pc < 0x4000) are JobApi calls and stay in the capture. EDGE jobs end at the return to 0x3368 (format v2; v1
  missed that return and ran on to the module's exit, see progress log).
- **File:** one per job, `job_<wid>_<n>.bin`: header, LS snapshot, registers, then the event records.

## Replay (`SPU_JOB_REPLAY=<dir>`, a mode of the game exe)

- Starts the SPU side only: registry, images, overlays. No PPU and no GPU.
- Builds a context from each capture and runs it from the snapshot pc.
- Serves GETs, atomics and channel reads from the transcript, in order. A request that differs from the transcript
  (another ea or size, an extra read) is reported as divergence at that point.
- Compares every PUT and the final LS against the transcript and reports the first differing byte.
- **Gate:** the current lifted code must replay every captured job byte-identically before the harness certifies
  anything. A nondeterministic job means the transcript misses an input. Fix the harness, not the job.
- It also times the replay per job and per function: the stage map below.

## Stage map (to fill from replay timing)

| function | share of EDGE samples | stage | native status |
|---|---|---|---|
| `E550` | 13.4% | skinning (4 bones/vertex, 2 vertices/iteration) in its loop at `loc_EC08` | loop native in production (v3, default): E550 5.59 -> 2.59 ms, jobs 35.8 -> 32.8 ms, in game +8.0/+8.8% |
| `73E0` | 11.8% | ? | |
| `A880` | 5.8% | ? | |
| `D640` | 3.6% | ? | |
| `F538` | 3.6% | ? | |

## Progress log

- 2026-10-24: design. No SPU job capture/replay existed in the toolkit; the nearest pieces are `SPU_JOBEXEC_DUMP`
  (LS at first job entry), `g_spu_put_hook` and `spu_interp_diff_test.c` (lifted vs interpreter on one context).
- 2026-10-24: capture built (toolkit `runtime/spu/spu_channels.c` + `spu_dma.h`; the file format is in the header
  comment of the "SPU job capture" block). Reader: `analysis/jobcap_dump.py <file> [-v]`.
  - **Beyond the plan:** a 32-byte record per event; a POLL event (the HLE selectWorkload writes LS 0x100..0x17F and
    r3); a PMCALL event for every dispatch into PM code during the job; an END record carrying GPRs and the full LS
    at the end, which replay compares against. The header also stores `resident_task`, `policy_mode`, the
    `resident_code` slots and the capture time.
  - **Job end is not "first pc < 0x4000":** jobs call JobApi in PM code through the dispatcher (19 such dispatches
    per EDGE job), so that rule would cut every job at its first call. The capture closes when the job dispatches
    to the link it was entered with, at `spu_halt`, or at the next job start on the thread. Measured: the EDGE job
    never returns to its link (0x3368). It leaves by a PM dispatch to 0x2F5C with r0 = 0. The PM then completes the
    job (GETLLAR/PUTLLC at 0x35E0), polls once (answer 0) and exits to the kernel. So all 200 captures end at that
    halt (pc 0x9C0) and hold exactly one job plus the PM tail. Within a file, the job proper ends at the PMCALL to
    0x2F5C.
  - **Run cap_1** (`SPU_JOB_CAPTURE_N=200 SPU_JOB_CAPTURE_T0=600`, runCap exe): 200 files in 344 ms of game time
    (t = 600.0-600.3 s). Workloads: wid 17 x123, wid 16 x77, wid 15 x0 in that window. 198 jobs are overlay 225,
    entry 0xAD98; one is overlay 220 (entry 0x4270) and one overlay 205 (entry 0x4138). Files are 531-568 KB
    (median 537 KB; two LS copies make up 512 KB of that), 108 MB in total. Per job: 47-86 events (median 60), GET
    0-4 (median 0: the PM stages job inputs before entry, so they are in the LS snapshot), PUT 0-5 (3), GETLLAR 2-4,
    PUTLLC 1-3, PUTLLUC 0-2, RDCH 24-50 (32), POLL 1, PMCALL 16-19. Heartbeats kept rising through and after the
    capture (game_frame 4160 at 575 s, 4613 at 695 s), and the log shows no crash.
  - **Not recorded:** rchcnt values, interrupts, PPU stores between the job's own transfers. `spu_id` reads 0 in
    every capture (the policy contexts' field, not the physical SPU).
- 2026-10-24: replay built (toolkit `runtime/spu/spu_job_replay.c`, format structs in `spu_jobcap.h`; port `main.cpp`
  calls `spu_job_replay_run` right after `gow_mlaa_hle_register`, before the ELF loads).
  - **How it runs a job:** one context, loaded from the header, the state block, LS and GPRs. It enters at the snapshot
    pc through the PM's own `bisle` shape (`host_depth++`, dispatch, `spu_drain_call(link)`) under
    `spu_run_with_halt`, on a thread with a 256 MB stack. Hooks sit next to the capture's: `mfc_do_transfer` (after
    the request-only checks, before any guest memory access), `spu_mfc_atomic`, `spu_rdch`, `spu_rchcnt`, the
    PMCALL/POLL/job-start sites in `spu_indirect_branch`, `spu_halt`, `spu_take_interrupt` and every trampoline hop.
    Each request must be the next recorded event (type plus cmd/ea/lsa/size, channel or target), or the job stops
    with a divergence report. A GET still calls `spu_overlay_note_get`, so EDGE fragments become resident as live.
    RDCH of a pure-state channel (13, 11, 15, 12, 24, 27, 25) also checks the context's own value and prints a note
    when it disagrees: the detector for a field the state block misses. PUT/PUTLLC/PUTLLUC payloads are compared;
    at the end, GPRs then all 256 KB of LS against the END record, with the first differing byte.
  - **Harness fixes the gate forced (capture format v2):**
    1. *State block.* v1 had none of `int_enable` (1 at every EDGE job start), `event_mask` (0x2), `srr0`, the MFC
       staging/tag registers, the engine's `tag_completed`, the reservation, the parked lists, `host_depth`. A fresh
       context with `event_mask = 0` never takes the PM's SN interrupts. v2 stores them; replay refuses v1.
    2. *New events.* IRQ (srr0, event status/mask, hops since the previous event; replay re-raises those bits at that
       hop, for an Lr a peer raises) and RCHCNT (`rchcnt SPU_RdEventStat` polls the reservation against guest memory).
    3. *Job end.* All 200 v2 captures first diverged at the same spot: the PM's code after the job, at 0x2D18. The
       capture had kept recording there. The job does return to its link 0x3368, but through the entry bracket's
       `spu_drain_call`, never as a dispatch, so v1's "dispatch to link" rule never fired. The run went on through the
       PM's tail (0x2F5C dispatch, completion GETLLAR/PUTLLC at 0x35E0, two SN interrupts, a poll, exit at 0x9C0).
       That tail runs in two PM host frames above the bracket (`host_depth` 3 at job start). The replay does not
       build those frames (it would need their return pcs), so a `bi $r0` that host-returned in the game dispatched in
       the replay. Scope decision: the gate covers the job proper, not the PM's completion tail. The capture now closes when that
       drain returns at the link at the bracket's depth (`spu_job_drain_return` in `spu_drain.c`), and the replay ends
       at the same drain return.
  - **Run cap_2** (same command as cap_1, `work/jobcap2`, v2): 200 jobs, all wid 16, overlay 225, entry 0xAD98, all
    END "return to link" at 0x3368, t = 600.0-600.2 s, 104 MB. Events over all 200 jobs: GET 17, PUT 617, GETLLAR
    417, PUTLLUC 217, RDCH 3204, POLL 200, PMCALL 1600, IRQ 0, RCHCNT 0. Every interrupt and `rchcnt` was in the PM's
    tail. Per-function timing (the stage map) is not in the replay yet.
  - **Gate: 200/200 identical** (`[replay] 200/200 jobs identical`, exit 0, no live-state notes). This covers one
    path only: wid 16, overlay 225, END by return. Not seen by the replay yet: wid 17 jobs, other overlays (cap_1 had
    220 and 205), jobs ending by halt or by the next job start. Not verified: the replay side of IRQ forcing and of
    RCHCNT serving. Both are recorded (the tail captures have them), but cap_2 has none inside a job. A wider
    re-capture (a longer window, `SPU_JOB_CAPTURE_WIDS`) should come before the harness certifies other jobs. Negative check on
    corrupted copies: a flipped decrementer value, GET byte or start-LS byte and a changed recorded PUT byte or
    end-LS byte each FAIL, naming the final r3, the PUT (event, ea, +offset) or the LS offset.
  - **Replay time per job** (QPC around `spu_run_with_halt`, load and compare excluded): single run median
    0.054-0.084 ms over three runs (min 0.031, max 0.70-1.85, the first jobs are cold). `SPU_JOB_REPLAY_REPEAT=20`:
    median of medians 0.071 ms, min 0.028, max 0.270, p10/p90 0.045/0.155. That is well under the 0.21 ms per job
    measured in game. The in-game figure includes the PM's own work around the job, other kernel threads competing,
    and cold caches; the replay figure is the job proper, warm and alone. Use REPEAT medians, and compare A/B on the
    same machine state.
- 2026-10-24: hook cost and the in-game split.
  - **Hook cost:** with the env unset, the capture/replay checks on every transfer, channel op and hop cost 1.1% level
    fps (swapped A/B, `NOPMPROF=1`: tail 3.221/3.217 vs 3.258/3.254). The hooks now build only with
    `-DPS3RECOMP_SPU_JOBLAB=ON` (toolkit CMake option, default off). Production exes are built without them; lab exes
    are staged as `build_runLab`.
  - **Job proper vs module run, in game** (`[pm-jobt]`, lab build, `SPURS_PM_PROF`, 60 s windows in the level): EDGE
    wids 15-17 each run ~133k jobs/min at 144 us per module run, of which 116 us (81%) is the job proper (job start to
    its return to 0x3368). p50 <= 70 us, p90 <= 280 us. So the policy module's own work around a job is ~19%; the job
    code is the target, and the native-stage plan stands. The replay median (56-71 us) matches the in-game p50.
    Why the in-game mean (116 us) is above cap_2's replay mean (66 us) was answered by cap_3 below: mostly job mix.
  - **Wider sample:** `SPU_JOB_CAPTURE_EVERY=<k>` keeps one candidate job in k, so N jobs spread over minutes and all
    three workloads.
- 2026-10-25: cap_3, the replay profile, and register promotion (closed).
  - **cap_3** (`SPU_JOB_CAPTURE_N=400 SPU_JOB_CAPTURE_EVERY=2000 SPU_JOB_CAPTURE_T0=300`, lab exe, `work/jobcap3`):
    400 jobs over minutes, wid 15 x133, 16 x138, 17 x129. All overlay 225, entry 0xAD98, END by return to 0x3368.
    Still no IRQ or RCHCNT inside a job, so the replay side of those stays unverified. **Gate: 400/400 identical.**
  - **Replay vs game:** REPEAT=20 mean 99 us per job (median 62) against 116 us in game. The gap to cap_2 was job
    mix; in-game contention is ~17%. (The replay also pays the per-hop replay hooks, so that slightly understates
    contention.) A native stage's replay gain should carry over to the game mostly intact.
  - **Replay profile** (`PS3_SAMPLE=1 PS3_SAMPLE_HOST=1`, REPEAT=500; the sampler starts before the replay): same
    ranking as in game. `E550` and `73E0` are ~17% of job-code samples each, then `D640`, `A880`, `F538`, `D250`,
    `DCC0`. E550 is float-heavy (162 float ops, 209 LS reads, 79 LS writes), has no indirect branch, and is called
    from 4 sites. Its callees are `B600`, `B638`, `B648`, `B650`, `B660` and `BBC8` (x4). 73E0 has one indirect
    branch.
  - **Register promotion, tried and closed** (`analysis/spu_regpromote.py`): keeping SPU registers in C locals
    between calls is bit-exact (400/400), but gains at most ~5%, inside the noise. SPU code keeps ~100 registers
    live against 16 x86 vector registers, so the locals spill to the stack: E550 grew from 13.5k to 22.9k x86
    instructions, with 3.6x the stack accesses. That supports the native path, where the live state is the
    algorithm's and not the SPU register file.
  - **Timing harness:** the replay thread is now pinned (core 2, high priority) and prints the sum of per-job
    minimums. Three runs of one build: 36.2 / 37.0 / 36.3 ms over 400 jobs (REPEAT=50), against +-10% unpinned.
    That is the A/B figure for native stages. `work/buildlab.sh <dest>` builds a lab exe into `build_<dest>` and
    puts `build_v017` back to production.
  - **Next:** an E550 boundary probe in a lab lift: the arguments and LS ranges written per call, plus the
    function's own time. Its exit snapshots become the gate for a native E550.
- 2026-10-25: E550 boundary probe (lab lift `work/spu_recomp_d935.fnp_e550.c`: E550 wrapped, reads of E550 and
  its callees B600/B638/B648/B650/B660/BBC8 routed; replay of cap_3 still 400/400 identical).
  - **Calls:** 166 over the 400 jobs. 151 jobs call it (136 once, 15 twice). `r3` is always 0x107A0, a
    job-context block. `r4` is a buffer that E550 rewrites in place, exactly `r5 * 48` bytes in all 166 calls
    (48 bytes = three quadwords per element; `r5` = 1..183, median 20). `r6` is a second input region. `r7` takes
    1, 7, 0, 0x0103041F; `r8` is mostly 0.
  - **Where the time goes:** not the `r4` elements. The time per call does not grow with `r5`: about 50-95 us
    fixed by `r7` class. It tracks how much the call writes in LS 0x28000-0x30000 (buffers at 0x29000, 0x2B700,
    0x2C100 and 0x2EB00; corr 0.77, ~4 us/KB) and its total writes (corr 0.82). Those buffers are the stage's real
    output. Their layout is the next thing to decode. Probe times include the routed reads, so they read high.
  - **Gate data:** `work/fnp_e550/` holds entry and exit GPRs+LS for all 166 calls (game data). A native E550 must
    reproduce every exit state from its entry state.
- 2026-10-25: E550 re-timed without read routing, and its hot loop.
  - **Unrouted** (`spu_fnprobe_lift.py --no-reads --blocks`, REPEAT=20, `[fnpt]` per-call minimum): 166 calls,
    5556 us in total, 33.5 us mean. That is ~15.5% of the 35.8 ms summed per-job minimum of all 400 jobs (the
    wrapper alone costs nothing: 35.8 vs 36.2-37.0 ms).
  - **Block map** (`[fnpb]`, counts x lifted lines): the loop at `loc_EC08` (125 lifted statements) is 75.4% of
    E550's executed work. Then `F104` 9.2%, `BD54` (in callee BBC8) 8.9%, `E804` (the r5-times loop over the r4
    elements) 3.5%. Per-call time is linear in executed lines (corr 0.998, ~1.2 us per 1000 lines), with no fixed
    cost to speak of: the earlier "50-95 us fixed" was the routed reads.
  - **The loop** (to verify): matrix-palette skinning, 2 vertices per iteration (r48 -= 2). Byte bone indices x48
    pick 3-row matrices at bases r34/r35/r37; weights are `cuflt` of bytes x r91; the four matrices are blended with
    fm/fma; 96 bytes per iteration go to r49 and 32 to r86. Software-pipelined: the next iteration's weights are
    shuffled in this one.
  - **Float helpers:** with `SPU_FP_NATIVE` each fm/fma is the native op under round-toward-zero plus validity
    masks. The exact per-lane fallback is rare (`spu__x8_fma_fix` 0.3% of replay samples). A native loop can reuse
    the same helpers and stay bit-exact.
- 2026-10-25: the E550 loop at `loc_EC08` native, in a lab lift (first native stage).
  - **Pieces:** `port/gow_edge_native.c` (an SPU source: same flags and defines as the lift; built only when
    the d935 lift is present). Since the in-game A/B below it is in production: see the entry after this one. The lifted loop body it
    reuses is game code, so the repo has none of it: `port/gen_edge_e550_body.py <lift> <out.h>` writes
    `gow_edge_e550_body.h` (`GOW_E550_REGS/LOAD_INPUTS/STORE_OUTPUTS/BODY`) from the d935 lift into the build
    tree, and CMake reruns it when the lift changes. `port/gen_edge_native_lift.py <lift> <out>` (then `analysis/edge_native_lift.py`) inserts, just before
    `loc_0000EC08:` in E550 (so once per loop entry, not per iteration),
    `if (g_edge_native) { if (g_edge_native <= 2) { gow_edge_e550_ec08(ctx); goto loc_0000EDE8; } check_begin }`,
    and a `check_end` after `loc_0000EDE8:`. `g_edge_native` is `GOW_EDGE_NATIVE`, read once: unset = lifted,
    1 = native v3 (`GOW_EDGE_V2=1`: v2), 2 = native v1, 3 = check v3/v2, 4 = check v1. It composes with
    `spu_fnprobe_lift.py` in either order. Lab exes: `build_runLabEN` (lift `work/spu_recomp_d935.edgenat.c`) and
    `build_runLabENP` (plus the `--no-reads --blocks` E550 probe).
  - **What the loop computes** (from the lift, plus the loop constants, identical in all 606 loop runs of
    cap_3): matrix-palette skinning, 2 vertices (A, B) per iteration, r48 -= 2 until 0, at least once.
    - Bone stream at r51, 16 bytes per pair: 8 halfwords, byte 0 = weight, byte 1 = bone index; A = halfwords
      0-3, B = 4-7. Weight = `cuflt(byte, 155) * r91` (r91 = 0x3B808080 = 1/255), splat per bone by shufb (r81,
      r95, r94, r93 = words 0-3). Offset = index * 48 (andhi 255, shli 4 + shli 5).
    - Palette: one 48-byte 3x4 matrix per bone, rows at r37, r35 = r37 + 16, r34 = r37 + 32 (all 606 loop
      entries; r37 = 0x144C0 in the four printed). Each blended row = w0*M[b0] + w1*M[b1] + w2*M[b2] + w3*M[b3]
      (fm, then three fma).
    - Out 1, r49 (0x2EB00 in the runs printed, +96 per pair): the blended rows in palette order, lanes 0-2
      kept (r84 = FFFFFFFF x3, 0), lane 3 = r97 = 1.0 in row 0, 0 in rows 1 and 2.
    - Out 2, r86 (+32 per pair): the vertex input from r50 (16 bytes each, X = (x,y,z,w)) transformed:
      P = X*row0' + Y*row1' + Z*row2' + T with Y = (y,z,x,w), Z = (z,x,y,w) (r36, r82), T = (row0.w, row1.w,
      row2.w, 0) (r70, r69), in that order. Lane l = in[l]*row0[l] + in[l+1]*row1[l] + in[l+2]*row2[l] + row_l.w
      (indices mod 3): the palette stores the 3x3 part skewed by lane, translation in lane 3. Lane 3 of P is w.
      r86 = r50 - 32 at entry (all 606 entries), so each result lands on its own input (in place). The r86/r49 buffers are the
      0x2B700/0x2EB00 outputs the boundary probe saw.
    - Software pipelining: an iteration loads the next pair's bone quadword and inputs and forms the next
      offsets, weights and input rotations; within an iteration every load precedes every store.
  - **Loop counts** (cap_3, one pass): E550's 166 calls enter the loop 606 times (E550 loops back to `EA0C`),
    27980 iterations, 55960 vertices.
  - **Liveness at `loc_EDE8`** (script over E550's body: labels/gotos; a call reads r0, r1, r3-r10, r80-r127;
    `SPU_RET` reads r0-r3, r80-r127): of the 57 registers the loop writes, r4 r5 r6 r8 r10 r80 r83 r86 are live.
    If calls and halts read every register, r11-r28, r30, r31, r33, r61, r78 too. The job replay compares all
    GPRs at job end, so every native version leaves all 57 with the lifted loop's final values (pipeline values
    for the iteration that never runs included).
  - **Versions** (all bit-exact by construction; every float result is the same op on the same operands):
    - v1: the lifted body statement for statement (the generated header: `ctx->gpr[N]` -> locals,
      direct LS access), same helpers.
    - v2: A and B packed in one 256-bit vector. All iterations but the last run on packed state; the last one
      runs the lifted body from that state, so the 57 registers come out exactly. shufb with a loop-invariant
      control becomes one pshufb per operand, built from the control's value (any control). fm/fma 8 lanes at a
      time with the SPU_FP_NATIVE per-lane rule; if any lane fails, that half is redone by `spu_fm`/`spu_fma`.
      Odd or zero r48, or fewer than 2 iterations: v1.
    - v3: v2 with the lane rule checked once per iteration: compute speculatively, keep the iteration if every
      lane of every op passes, else recompute it with v2's ops (nothing is stored before that decision). Inputs
      are checked as loaded (matrix rows, next inputs); op results are not rechecked where they feed the next op
      (they are ordinary or zero when the iteration passes) and their zero lanes are the op's zero-factor lanes.
      Shuffles that move whole words, word masks and checked constants keep that property; their form is
      verified at entry (else v2). `cuflt(x, 155)` of a word below 2^24 (verified on r77/r102) is the plain
      int-to-float conversion. Per cap_3 pass: 40 of 27980 iterations recomputed exactly, 171 8-lane ops redone
      by the 4-lane helpers.
  - **Gates** (`build_runLabEN`, the gate command of the task): `GOW_EDGE_NATIVE=1` (v3):
    `[replay] 400/400 jobs identical`; unset: `[replay] 400/400 jobs identical`; v2 (`GOW_EDGE_V2=1`) and v1
    (`=2`): 400/400. Check mode (all 128 GPRs and the whole LS compared at `loc_EDE8` after each loop run):
    v3, v2 and v1 each `606 calls checked, 0 differ`. Negative tests (`GOW_EDGE_FAULT`, first native run only):
    - LS bit (1): job gate `job_15_3.bin FAIL ... final LS differs first at 0x2BB2F: recorded 00, replay 01`,
      `399/400`; check mode `call 0: LS differs in 1 bytes, first at 0x2BB2F (lifted 00 native 01)`,
      `606 calls checked, 1 differ`.
    - r5 bit (2): job gate 400/400, it does not see it; check mode `call 0: r5 lifted BEAFD312 ... native
      BEAFD313 ...`, `1 differ`. r5 is live at `EDE8` by the liveness above, yet overwritten before every job end
      on the paths cap_3 takes. So the job gate is weak for the loop's exit registers: that all 57 come out
      exactly rests on check mode, which compares every register at the loop exit.
  - **Timing** (pinned replay, `SPU_JOB_REPLAY_REPEAT=50`, sum of per-job minimum, ms; one exe, env on/off,
    alternated): lifted 35.784 35.732 35.719 35.963 35.819 38.205 35.799 35.671 (median 35.79); v3 33.008 35.382
    32.750 32.655 32.760 32.714 32.794 32.966 (median 32.78, -8.4%); v2 33.939 34.017 34.045 33.998 (-5.0%);
    v1 35.273 35.267 37.586 35.254 (-1.5%). 38.205 and 35.382 are single outliers. The last lifted/v3 pair ran on
    the final rebuild, whose only change is check-mode output.
  - **E550 alone** (`build_runLabENP`, `SPU_FN_PROBE=E550 SPU_JOB_REPLAY_REPEAT=20`, sum of the 166 per-call
    minimums, us): lifted 5583 6151 5602 (5555 5907 5571 on the previous build of the same lift); v3 2593 2632
    2591 (2.16x); v2 3878 3880 3898; v1 5164 5160 5189. With `--blocks` the lifted loop also pays one counter
    increment per iteration that the native path skips.
  - **The loop alone** (check mode, rdtsc around each loop run, REPEAT=10): lifted 601-612 ticks per iteration,
    v1 554, v2 381-392, v3 222-228 (2.7x). v2's per-op lane checks were ~3/4 of its ~1000 instructions per
    iteration; v3's iteration is ~550 instructions, with some spills (16 ymm registers).
  - **Seen on the way, not changed:** clang-cl ignores `-ffp-contract=off` (`unknown argument ignored`, 150 times
    in `buildv017.log`), so the SPU sources build with clang's default contraction. The SPU helpers use intrinsics
    and the gates pass; whether any scalar fallback expression is contracted is not verified. The clang-cl
    spelling is `/clang:-ffp-contract=off` (fixed: see below).
- 2026-10-25: E550 native in game, then in production (default).
  - **In-game A/B** (x31): a production exe (job lab off) built with the switched lift, `GOW_EDGE_NATIVE=1` vs
    unset (then lifted), two copies of the same exe, swapped pairs, no PM profiling: window 3.572 vs 3.309
    (+8.0%) and 3.576 vs 3.287 (+8.8%) game frames/s. The replay predicted -8.4% of job time, so the per-job
    minimum sum of the pinned replay tracks in-game speed: the next stages can be sized on the replay.
  - **Production:** the port's CMakeLists.txt builds the d935 lift through `port/gen_edge_native_lift.py`
    (output `spu_recomp_edge.c` beside the lift, untracked; the original drops out of the sources) and
    generates `gow_edge_e550_body.h` into the build tree. `GOW_EDGE_NATIVE` unset = native v3, 0 = lifted,
    2/3/4 as before. The script copies a lift that already has the switch unchanged, so lab lifts made with it
    still swap in. Gates on the lab build of that tree (`build_runLabEN3`): unset and `=0` both
    `[replay] 400/400 jobs identical`.
  - **In-game check** (x33, production exe, `GOW_EDGE_NATIVE=3`, one 1100 s level run): `2252800 calls
    checked, 0 differ; 106540596 iterations`; loop constants equal in every call; rows at r37/+16/+32 and
    r86 = r50 - 32 in all 2252800; fallbacks 0 to v1, 0 to v2; 149665 iterations recomputed exactly (0.14%).
    The check reports every 4096 calls now (a killed run never reaches atexit). In the check's rdtsc the
    loop is 323 vs 851 ticks per iteration (2.6x), close to the replay's 2.7x.
  - Not covered by any run: the odd-count (v1) and unexpected-control (v2) entries; both are the lifted
    body or v2's checked ops, so correct by construction.
- 2026-10-25: `-ffp-contract=off` now reaches the SPU sources. clang-cl ignored the bare flag, so lifted SPU
  code (built with FMA, `-march=x86-64-v3`) was compiled with clang's default contraction of `a*b+c` inside a
  statement. The port passes `/clang:-ffp-contract=off` under clang-cl now. The exe changes; cap_3 replay is
  still 400/400 (native default and `=0`), so contraction never changed these jobs' results, and the replay
  time is neutral (sum of per-job minimum, alternated: before 32.556 33.472 32.583 32.793, after 32.734 33.469
  32.680 32.633 ms). The 150 remaining `unknown argument` warnings are toolkit runtime files, built with
  `-msse4.1` (no FMA), where nothing can contract. PPU lifted code has no `-march` either.
