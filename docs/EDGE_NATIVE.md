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
| `73E0` | 11.8% | per-vertex light accumulation (8-vertex groups x r83 lights, two passes) in the region `loc_79B0`..`81C0` | region native in production (v3, default): 73E0 6.11 -> 3.03 ms, jobs 33.3 -> 30.1 ms (-9.5%), in game +7.8/+7.9% |
| `A880` | 5.8% (replay sampler: 7.2% of job time) | per element, the cross product of two input quads scaled by the second's w and normalized (8 elements/iteration) in its loop at `loc_A950` | loop native in production (`GOW_EDGE_A880`, default): A880 ~1996 -> 375 us, jobs 28.34 -> 26.79 ms (-5.4%, replay), in game +5.6/+5.0% |
| `D640` (+ callee `D250`) | 3.6% (+ `D250`; replay sampler: ~10% of job time together) | triangle culling with index compaction (4 triangles/iteration) in the loop at `loc_D92C`; `D250`'s loop at `loc_D4DC` transforms the vertices to screen space with outcodes (4 vertices/iteration) | both loops native in production (`GOW_EDGE_D640`, default): D640 incl. D250 3110 -> 1189 us, jobs 29.61 -> 27.62 ms (-6.7%, replay), in game +2.9/+4.2% |
| `F538` | 3.6% (replay sampler: 5.5% of job time) | per input quad, a 4x4 transform by four loop-invariant columns (8 quads/iteration, mostly in place) in its loop at `loc_F620` | loop native in production (`GOW_EDGE_F538`, default): F538 ~1349 -> 536 us, jobs 26.45 -> 25.63 ms (-3.1%, replay), in game +2.3/+3.3% |
| `DCC0` | replay sampler: 4.4% of job time | a serial integer decode (cntb-driven variable-length steps through LS tables, plausibly index decompression: not verified) in its loops at `loc_E210` and `loc_E3C4` | probed, not staged: no float work and each iteration's addresses depend on the previous one's loads, so only a v1 applies (expected ~0.7% of job time); deferred |
| `6AC8` | replay sampler: 3.7% of job time | per group of 8 vertices (gathered by index), a smoothstep-shaped falloff around each of 1-3 spheres, squared and weighted, accumulated into the output quads (meaning not verified) in its loop nest at `loc_6BE0`/`loc_6DE0` | loop nest native in a lab build (`GOW_EDGE_6AC8`, default lifted): 6AC8 ~920 -> ~370-460 us, jobs 26.03 -> 25.47 ms (-2.2%, replay); not in game yet |

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
- 2026-10-25: 73E0 boundary probe and block map (lab lift `work/spu_recomp_d935.fnpb_73e0.c`:
  `spu_fnprobe_lift.py --no-reads --blocks`, 73E0 and its callees 5D90 6AC8 7040 B5F0 B600 B670; exe
  `build_runLab73P`; cap_3 still 400/400).
  - **Calls:** 118 over the 400 jobs, one per calling job (282 jobs never call it). `r3` is always 0x107A0 (the
    job-context block E550 gets too), `r8` 0, `r9` 0x29000; `r4` `r5` `r6` `r7` `r10` are buffers (37-38 distinct
    values each). Writes per call: 1.2-41.9 KB (median 7.9 KB), in scratch at 0x12xxx/0x13xxx, the output buffers
    0x29000-0x37xxx, and the stack.
  - **Time** (`[fnpt]`, per-call minimum, REPEAT=20): 6126 us in total, mean 52, median 26, max 245 us: 18.8% of
    the 32.67 ms summed per-job minimum. Per-call time tracks the inner iterations (corr 0.99) and the bytes
    written (0.95).
  - **The indirect branch:** lifted 73E0 has no `spu_indirect_branch`. Its only exit other than `SPU_RET` is the
    tail call to 6AC8 at the end of the `821C` block (taken when LS[r1+0x40] != 0), lifted as a trampoline return.
    It was never taken in cap_3: `076F0` (the `SPU_RET` epilogue) is entered 118 times, from `821C` each time, and
    `06AC8:118` is the direct call at 0x8200. The lift also has function 75D8, a second copy of 73E0's blocks from
    `loc_75D8` on (an alternate entry, reached only through dispatch); it stays lifted.
  - **Block map** (counts x lifted statements per block, 5.03M in all): the outer loop `loc_79B0` (1869 iterations
    per pass, 8 vertices each) with its two inner loops is 76.1%: `7BB4` (inner loop 1, 127 statements) 38.0%,
    `7ED8` (inner loop 2, 93) 27.8%, and the outer blocks `79B0` 4.7%, `8058` 3.1%, `7DC0` 2.5%. Both inner loops
    run r83 times per outer iteration (15056 iterations each per pass; r83 = 3-42 per call, mostly 6-10). Outside
    the region: callee 6AC8 ~12.6% (`6DE0` 6.3%, `6BE0` 4.6%), the index-table loops `7670` 4.5% and `75D8` 4.1%,
    `83E8` 1.6%. No calls inside the region.
- 2026-10-25: the 73E0 region (`loc_79B0` to `loc_81C0`) native, in a lab build (`build_runLab73EN`).
  - **Pieces:** `port/gow_edge_native_73e0.c` (an SPU source like `gow_edge_native.c`), behind `GOW_EDGE_73E0`
    (read once): unset/0 = lifted (now 0 = lifted, unset = native: see below), 1 = native v3 (`GOW_EDGE_73E0_V2=1`: v2), 2 = native v1, 3 = check v3/v2,
    4 = check v1. `port/gen_edge_73e0_body.py <lift> <out.h>` writes `gow_edge_73e0_body.h` (`GOW_73_REGS/LOAD/
    STORE` and the segments `S79B0 S79B0T B7BB4 S7DC0 S7DC0T B7ED8 S8058`) from 73E0's copy of the blocks into the
    build tree; it checks that the lift has exactly the expected branches and labels and fails otherwise.
    `port/gen_edge_native_lift.py` now adds each switch only where it is missing (per loop, no longer
    `"gow_edge_native_mode" in text`), so a lift that already has the E550 switch still gets the 73E0 one; in
    73E0 (or its `_impl`) it inserts `if (g_edge_73) { if (g_edge_73 <= 2) { gow_edge_73e0_79b0(ctx); goto
    loc_000081C0; } gow_edge_73e0_check_begin(ctx); }` before `loc_000079B0:` (entered only by falling through
    from the 7890 block; the back edge jumps to the label) and `check_end` after `loc_000081C0:` (also reached
    by the skip at 7890 when r107 = 0). The port's CMakeLists builds the new source and header like E550's.
  - **What the region computes** (a reading of the lift and of the region constants, identical in all 118
    entries of cap_3; to verify): per outer iteration, 8 vertices are gathered through 8 offsets (lane 3 of the
    quads at r103, +128 per group) and transposed into SoA quads, vertices 0-3 / 4-7 (the lift's paired
    registers): positions (r66|r65, r62|r63, r60|r61), normals (r52|r50, r54|r53, r56|r55).
    - Inner loop 1, over the r83 lights (16-byte records at r99: position and w = 1/radius; r97: colour, scaled
      by r93 = 0x3F7EFEFE; r98: direction), software pipelined: d = P - V, d2 = d.d + 1e-4 (r84 = 0x38D1B717),
      rsq = fi(frsqest(d2)) for light j+1 while light j finishes. n = d * rsq, or the light's direction for the
      first r100/16 lights (the shufb control r9 is r104 = all of the second operand while j < that count, else
      r64 = LS[0xF920] = all of the first); n goes to the scratch at r23 (r105 + 96j). t = min(d2 * rsq * w, 1),
      s = 1 - t, a = (s*s*(3 - 2s))^2 * colour.w (r101 = 3, r102 = 2); colour.xyz * a goes to the scratch at r29
      (r106 + 96j). wrap = min(|0.5 * (N.n) + 0.5|, 1) (r72 = 0.5, r44 = sign mask, r33 = 1.0), weight =
      |a| * wrap; the weights are summed (fa) and n * weight accumulated (fma): 8 accumulators.
    - 7DC0 normalises the accumulated direction (frest/frsqest + fi) into N2 = (r71|r70, r69|r68, r67|r64).
    - Inner loop 2, over the same lights, reads both scratch buffers back (light j+1 loaded in iteration j):
      c = clamp(N2.n, 0, 1) (fcgt/selb against 1.0, and with cgti 0), v = colour.xyz * a * c, and the positive
      and the negated parts of v's three components summed into six accumulators per vertex (fa).
    - 8058 transposes back and stores 3 x 8 quads per group to r73/r74/r75 (+ the 8 offsets).
  - **Liveness** (script over the generated segments): after loop 1 the region always runs S7DC0, S7DC0T,
    loop 2 (same count, at least once) and S8058, which overwrite every register loop 1 writes except its 8
    accumulators (r42 r45 r47 r48 r49 r51 r57 r58). After loop 2, S8058 overwrites all but 23 of loop 2's: the 12
    accumulators, r2 r3 r4 r23 r24 r25 (fa results that the accumulators copy), r10 r11 r12 (last-iteration
    values) and r66 r52 (last-iteration loads). So the packed loops run every iteration, the last too, and write
    back only those registers; the others stay stale until those blocks overwrite them, so at `loc_81C0`, where
    check mode compares all 128, every register is the lifted one. (E550's native loop leaves all 57 registers
    it writes exact; here the unit is the region.) The last iteration's next-light loads (one record past the
    last) feed only dead registers in loop 1 and are skipped; in loop 2 they feed r12, which the 4-lane helpers
    compute (fm, fma, fma, as the lift) from whatever is there.
  - **Versions** (every float result is the same op on the same operands):
    - v1: the region statement for statement, the SPU registers in C locals (the generated macros), direct LS
      access, the lift's control flow between the segments.
    - v2: v1 with both inner loops 8-wide (vertices 0-3 low, 4-7 high), every op with the SPU_FP_NATIVE lane rule
      and a 4-lane fallback per half. New 8-lane ops: fa/fs (zero results allowed), fnms, fcgt (the toolkit's key
      compare), frsqest (one gather; the 4-lane form's table select also kept), fi, cgti/and/xor/andc/selb.
      A self-test at mode init checks frsqest (both forms), fi and fcgt against the 4-lane helpers (200000
      quadword compares, 0 differ). Loop 1 loads the next light before storing the r23 quads (the lift stores
      first), so the packed loop runs only if the scratch it writes does not overlap the records it reads
      (else the lifted loop; 0 such entries in cap_3).
    - v3: v2 with one range check per iteration, as E550's v3 (min/max of every op result and LS input shifted
      left by one; allowed zero lanes out of the min, +0 out): an iteration that fails is recomputed with v2's
      ops before anything is stored. It needs whole-word shufb controls (then a splat is one vpermilps), the
      sign mask, r59 = splat-word-0 (so the r9 choice is one scalar compare), plain nonzero constants and plain
      vertex inputs; the region-invariant part is checked once per region entry, r64 when its value changes.
      Per cap_3 pass: 240 iterations recomputed exactly (both loops together), 240 loop-1 iterations run on the
      checked ops without a try (their carried values were not all plain), 0 entries to v2, 0 to the lifted
      loops, 0 ops redone by the 4-lane helpers.
  - **Gates** (`build_runLab73EN`, `SPU_JOB_REPLAY=work/jobcap3`): `GOW_EDGE_73E0=0`, unset, `=1` (v3), v2
    (`GOW_EDGE_73E0_V2=1`) and `=2` (v1): each `[replay] 400/400 jobs identical`. Check mode (all 128 GPRs and the
    whole LS compared at `loc_81C0` after each region run): v3, v2 and v1 each `[edge-73e0-check] 118 calls
    checked, 0 differ; 1869 outer iterations (8 vertices each), 15056 inner iterations per loop; region
    constants differ from the first call in 0 calls`. Negative tests (`GOW_EDGE_73E0_FAULT`, first native run):
    - LS bit (1, byte 15 of the last quad the last outer iteration stores): job gate `job_15_6.bin FAIL ... final
      LS differs first at 0x2B2FF: recorded 00, replay 01`, `399/400`; check mode `call 0: LS differs in 1 bytes,
      first at 0x2B2FF (lifted 00 native 01)`, `118 calls checked, 1 differ`.
    - r60 bit (2): job gate 400/400 (r60 is overwritten before every job end on cap_3's paths); check mode
      `call 0: r60 lifted 00000000 ... native 00000001 ...`, `1 differ`. As for E550, that the registers come out
      exact rests on check mode.
  - **Timing** (pinned replay, REPEAT=50, sum of per-job minimum, ms; one exe, env alternated):
    - lifted 33.270 33.217 33.350 33.384, v3 30.141 31.549 30.099 30.114 (median 33.31 vs 30.13, -9.5%);
    - a 4-way set: lifted 36.744 33.600 33.265 34.790 (noisy), v3 30.273 31.592 30.098 30.225, v2 31.379 31.440
      31.422 31.550, v1 32.560 32.601 32.521 32.638.
  - **73E0 alone** (`build_runLab73ENP`: the probe lift above through the switch; `SPU_FN_PROBE=73E0`,
    REPEAT=20, sum of the 118 per-call minimums, us): lifted 6112 6111 6164, v3 3028 3286 3025 (2.0x for the
    whole function; the region is 76% of it). The probe's `--blocks` counters are skipped by the native path.
  - **The pieces alone** (check mode, rdtsc, one pass): region native 8.6 M vs lifted 20.2 M ticks (570 vs 1342
    per inner iteration, 2.35x); inside it, packed loop 1 237 and loop 2 129 ticks per iteration (8 vertices).
    The rest, 3.1 M (~36%), is the outer blocks 79B0/7DC0/8058 in locals, no faster than lifted: the next piece
    of this region if it is worth it (~1.5% of the replay at 3x). An earlier build that rebuilt the shuffle
    tables and checked the constants at every inner-loop entry ran loop 1 at 472-529 ticks per iteration.
  - **Production:** the port's CMakeLists builds the switch and the native source into every exe; the default is
    the lifted region. Flipping the default (`GOW_EDGE_73E0` unset = native) is left for an in-game A/B and an
    in-game check-mode run, as E550 had (both done, now the default: see the entries below). Not covered by any run: the overlap fallback, the v2 fallback, the
    lifted-loop fallbacks, r116 = 0 with r107 > 0, and IRQ/RCHCNT inside a job (cap_3 has none).
- 2026-10-25: 73E0 in-game check (x35, production exe, `GOW_EDGE_73E0=3`, one 1100 s level run): `1146880 calls
  checked, 0 differ; 17445388 outer iterations (8 vertices each), 134513874 inner iterations per loop; region
  constants differ from the first call in 0 calls`. Unlike cap_3, the game exercised the fallbacks: 87 entries to
  v2 (unexpected constants), 2213516 v3 iterations recomputed exactly, 2214949 loop-1 iterations on the checked
  ops without a try, 8295 8-lane ops redone by the 4-lane helpers; all exact. Region rdtsc: 733.6 vs 1817.3
  ticks per inner iteration (2.5x).
- 2026-10-25: 73E0 native in game, then the default.
  - **In-game A/B** (x36): the production exe (E550 native, its default) in two copies, `GOW_EDGE_73E0=1` vs
    unset (then lifted), swapped pairs, no PM profiling: window 3.881 vs 3.600 (+7.8%) and 3.887 vs 3.602
    (+7.9%) game frames/s. The replay predicted -9.5% of job time.
  - **Default:** `GOW_EDGE_73E0` unset = native v3 now, `=0` the lifted region. Gates on the lab build of that
    tree (`build_runLab73D`): unset and `=0` both `[replay] 400/400 jobs identical`.
- 2026-10-25: D640 + D250 boundary probe and block map (lab lift `work/spu_recomp_d935.fnpb_d640.c`:
  `gen_edge_native_lift.py`, then `spu_fnprobe_lift.py --no-reads --blocks` on D640 counting D640 D250 B600, then
  a plain `spu_fnprobe_lift.py --no-reads` wrapper on D250 so it can be timed on its own (the probe filters by
  function); exe `build_runLabD6P`; cap_3 still 400/400).
  - **Calls:** 140 over the 400 jobs, one per calling job. All 140 take one path: D640 -> D678 -> D6A0 -> direct
    call of D250 (the indirect call at 0xD6B0, through word 1 of LS[r3 + 0x180], is never taken) -> D6C0 ->
    D81C/D820/D824 (r7 = 2) -> D8C8 -> loop `loc_D92C` -> DB70 (call B600) -> return. `r3` = 0x107A0, `r4` = 2,
    `r6` = 0x9640 in all 140; `r9` 87 distinct values (0x129xx-0x13xxx).
  - **Block map** (counts x lifted statements, 2.82M executed): D640's loop `D92C` (144 statements) 70.7%
    (13828 iterations, 4-390 per call), D250's loop `D4DC` (79) 27.5% (9812 iterations, 6-242 per call), D250's
    head 0.8%, everything else under 0.5% each. Both loops are straight-line bodies with only the back edge: no
    calls, no indirect branches.
  - **Time** (`[fnpt]`, REPEAT=20): D640 including D250 3071 us over 140 calls (10.4% of the 29.5 ms summed
    per-job minimum), D250 alone 1280 us. Linear in the loop iterations: D640's own time = 0.127 us per D92C
    iteration + 0.28 us (corr 0.997), D250 = 0.126 us per D4DC iteration + 0.33 us (corr 0.994).
- 2026-10-25: both loops native, in a lab build (`build_runLabD6N`; `build_runLabD6NP` with the probe lift).
  - **Pieces:** `port/gow_edge_native_d640.c` (an SPU source like the other two), behind `GOW_EDGE_D640` (read
    once, at whichever loop runs first): unset/0 = lifted (now 0 = lifted, unset = native: see below), 1 = native packed v3 (`GOW_EDGE_D640_V2=1`: v2),
    2 = native v1, 3 = check v3/v2, 4 = check v1 (all 128 GPRs and the whole LS at each loop's exit;
    `[edge-d640-check] D92C: ... D4DC: ...`, every 4096 calls per loop and at exit). `GOW_EDGE_D640_FAULT` (first
    native run of that loop): 1 = one LS bit in the first quad the last D92C iteration stores, 2 = bit 0 of r5
    after D92C, 3 = one LS bit in the first quad the last D4DC iteration stores, 4 = bit 0 of r24 after D4DC.
    `port/gen_edge_d640_body.py <lift> <out.h>` writes `gow_edge_d640_body.h` (`GOW_D6A_*` for D92C with
    BODY/TAIL around the exit test, `GOW_D6B_*` for D4DC) from D640's and D250's own copies (the alternate-entry
    functions D8C8, D92C and D4DC stay lifted) and checks the lift's shape. `gen_edge_native_lift.py` adds the
    third switch only where it is missing: in D640 (or its `_impl`) before `loc_0000D92C:` (`goto loc_0000DB70`)
    and `check_end` after `loc_0000DB70:` (also reached by the skip in the D6C0 block when r7 is not 1, 2 or
    3; not taken in cap_3), in D250 before `loc_0000D4DC:` (`goto gow_d4dc_exit`) and, after the back edge
    `if (r3 != 0) goto loc_0000D4DC;`, the new label `gow_d4dc_exit:` with `check_end`. The port's CMakeLists
    builds the source and header like 73E0's; the default is the lifted loops.
  - **What the loops compute** (a reading of the lift; to verify):
    - D4DC (in D250): per iteration one 64-byte record of 4 vertices (quads at r39/r33/r32/r29 + r23, r23 += 64
      up to 0), transposed to x, y, z, times a 3x4 matrix (12 fma) into clip space X, Y, Z, W; then 1/W (frest,
      fi, fnms, fma), the quotients with W's sign folded back (xor), a viewport scale and offset (fma) and
      W > 0; then six bound tests (fcgt) packed into outcodes (shufb, gbh) and stored with the screen values as
      four quads at r66/r37/r35/r34 + r21 (r21 += r31, r31 rotating by shufb r69). Pipelined three deep:
      iteration k transforms record k (A), divides record k-1 (B), tests and stores record k-2 (C).
    - D92C (in D640): triangle culling with index compaction, 4 triangles (24 bytes of halfword indices) per
      iteration, pipelined four deep: S1 loads the next group's index quads; S2 aligns the previous group's
      indices (shufb by r48, which alternates by xor r68 because 24-byte groups sit at alternating quad
      offsets), gathers 12 vertex quads at r29 + 16 x index, transposes them into the vertices' x and y, the
      edges (4 fs) and the AND / OR of the vertices' outcode words; S3 forms the signed area (fm, fms), its sign
      flipped by r63 and tested > 0, snaps the six coordinates (fma(x, r39, r38), cflts) and compares them for
      degenerate triangles, applies the outcode tests and makes a 4-bit cull mask (gb), plus the tail mask r66
      on the second-to-last iteration; S4 compacts the kept triangles' indices through LS shuffle tables indexed
      by that mask and the output alignment, merges the first quad with what LS holds and stores three quads at
      the output position r33 (+6 bytes per kept triangle; past r65 the stores go to stack scratch); r46 counts
      the kept triangles. The first two iterations' S4 are masked off (r31). The written ranges (in place,
      0x12xxx-0x13xxx, matching `r9`) suggest the index buffer is compacted in place.
  - **Versions** (every float result is the same op on the same operands):
    - v1: each loop statement for statement in C locals (the generated macros), direct LS access.
    - packed: pairs of iterations, the last one or two iterations through the lifted body from the pairs'
      state so every register the loop writes comes out exactly. D4DC: a pair (k, k+1) runs A on records
      (k, k+1), B on (k-1, k) and C on (k-2, k-1), 8 lanes each (a stage's low half takes the previous pair's
      high half). It loads record k+1 before iteration k's stores, so it runs only if the input records and the
      output quads (bounded from r31's words when r69 moves whole words) do not overlap; else v1. D92C: per
      iteration S4, S1, the gather addresses and the gathers run serially in the lift's order (LS reads and
      writes unchanged); S2's compute for (j, j+1) and S3 for (j+1, j+2) run 8 lanes wide, lane for lane, after
      both iterations' S4. The one fnms in S3 is mask arithmetic on 0 / all-ones lanes (not plain floats: the
      helpers take their exact per-lane path for it); it is a 4-entry table of the helper's own results for
      those operands, built once per entry (r55 must be 0 or all ones per lane, else v1). Loop-invariant shufb
      controls are pshufb (built once per distinct control); D92C's table-loaded controls use spu_shufb's rule
      without the byte swaps.
    - v3 (default packed) runs a pair speculatively with one range check (min/max of every float input from
      outside the pair and every op result, as E550/73E0 v3) and recomputes it with v2's ops before anything
      of it is used if a lane fails; the invariants feeding float ops are checked once per entry. v2 checks
      every float op per lane with the 4-lane fallback.
    - Per cap_3 pass: 13596 of 13828 D92C and 9532 of 9812 D4DC iterations packed (the rest is the lifted tail);
      156 D92C and 153 D4DC pairs recomputed exactly; 580 8-lane ops redone by the 4-lane helpers; 0 entries to
      v1 or to the checked ops.
  - **Gates** (`build_runLabD6N`, `SPU_JOB_REPLAY=work/jobcap3`): unset, `=0`, `=1` (v3), `=1` with
    `GOW_EDGE_D640_V2=1` and `=2` (v1): each `[replay] 400/400 jobs identical`. Check mode: v3, v2 and v1 each
    `[edge-d640-check] D92C: 140 calls checked, 0 differ; 13828 iterations` and `D4DC: 140 calls checked, 0 differ;
    9812 iterations`. Negative tests (`GOW_EDGE_D640_FAULT`):
    - 1 (LS, D92C): job gate `job_17_5.bin FAIL ... final LS differs first at 0x1294F: recorded 00, replay 01`,
      `399/400`; check mode `D92C call 0: LS differs in 1 bytes, first at 0x1294F (lifted 00 native 01)`,
      `D92C: 140 calls checked, 1 differ`.
    - 2 (r5, D92C): job gate 400/400; check mode `D92C call 0: r5 lifted 00000004 ... native 00000005 ...`,
      `1 differ`.
    - 3 (LS, D4DC): job gate `job_17_5.bin FAIL ... final LS differs first at 0x38FCF: recorded 00, replay 01`,
      `399/400`; check mode `D4DC call 0: LS differs in 1 bytes, first at 0x38FCF (lifted 00 native 01)`,
      `D4DC: 140 calls checked, 1 differ`.
    - 4 (r24, D4DC): job gate 400/400; check mode `D4DC call 0: r24 lifted 410C0C81 ... native 410C0C80 ...`,
      `1 differ`. As before, the job gate does not see register-only faults (both registers are overwritten
      before every job end on cap_3's paths); that the registers come out exactly rests on check mode.
  - **Timing** (pinned replay, REPEAT=50, sum of per-job minimum, ms; one exe, env alternated): lifted 29.614
    29.747 29.486 29.460 29.720 (median 29.61), v3 29.459 27.624 27.544 30.380 27.567 (median 27.62, -6.7%; 29.459
    and 30.380 are outliers, five earlier v3 runs of the same code before the V2 flag: 28.10 27.84 27.82 28.04
    28.03); v2 27.926 27.971; v1 28.929 28.880.
  - **The function alone** (`build_runLabD6NP`, `SPU_FN_PROBE`, REPEAT=20, sum of the 140 per-call minimums, us):
    D640 including D250: lifted 3163 3108 3110, v3 1189 1188 1293 (2.6x), v1 2514; D250: lifted 1273 1258 1257,
    v3 454 456 468 (2.8x), v1 915. Check-mode rdtsc per iteration varies 1.5x between runs on this machine
    (lifted D92C 532-672); within a run v3 is about 1.8x (D92C) and 1.5-2x (D4DC).
  - **Not covered by any run:** the skip path to DB70 (r7 not 1, 2 or 3), the v1 fallbacks (fewer than 3
    iterations, D4DC overlap, r55 not a mask), entries with non-plain invariants, IRQ/RCHCNT inside a job, the
    game itself (no in-game A/B or in-game check-mode run yet: next, as E550 and 73E0 had before their defaults
    flipped).
- 2026-10-25: D640 in-game check (x40, production exe, `GOW_EDGE_D640=3`, one 1100 s level run): D92C `917504
  calls checked, 0 differ; 89077101 iterations`, D4DC `917504 calls checked, 0 differ; 62229802 iterations`. Packed
  87562062 / 60394794 iterations, 0 entries to v1, 1270774 / 1029149 speculative pairs recomputed exactly, 4142436
  8-lane ops redone by the 4-lane helpers; all exact. Check-mode rdtsc 314 vs 724 (D92C) and 309 vs 685 (D4DC)
  ticks per iteration.
- 2026-10-25: D640 native in game, then the default.
  - **In-game A/B** (x41): the production exe (E550 and 73E0 native, their defaults) in two copies,
    `GOW_EDGE_D640=1` vs `=0`, swapped pairs, no PM profiling: window 3.989 vs 3.876 (+2.9%) and 4.057 vs 3.893
    (+4.2%) game frames/s. The replay predicted -6.7% of job time. Paired A/B measured over expected fps
    (expected = 1/(1 - job-time saving) - 1) falls stage by stage: E550 0.91, 73E0 0.75, D640 0.50. The
    single-instance profiled run after the flip (x43, next entry) shows the stage landed at full value, so the
    decay belongs to the paired measurement. Candidate cause (not verified): two instances on the 6-core/12-thread
    host are contention-bound, and the paired run absorbs a growing share of each saving (paired over single
    native rate: 3.88/4.565 = 85% after 73E0, 4.0/5.06 = 79% after D640). The paired swapped A/B stays the
    gate for a default flip (its pairs agree within ~1.5%); a stage is sized by single-instance ms/job.
    Later: A880's paired ratio came back at about 0.93 (x45), so the ratio is not a trend (0.91, 0.75, 0.50,
    0.93). D640's paired shortfall is unexplained rather than part of a decay; the sizing rule stands.
  - **Default:** `GOW_EDGE_D640` unset = native packed v3 now, `=0` the lifted loops. Gates on the lab build of
    that tree (`build_runLabD6D`): unset and `=0` both `[replay] 400/400 jobs identical`.
- 2026-10-25: where the frame goes after three stages (x43: production exe, all three native, one instance,
  1100 s, PM profiling on, `PS3_FTRACE=600:20`), against x38 (after two stages, same setup).
  - EDGE w15-17 busy: 96% of wall in both, so the frame is still bound by EDGE job throughput.
  - ms/job (pm-prof median over the tail windows): 0.1291 -> 0.1183 (-8.4%), against the replay's -6.7%: the
    D640 saving reached the game in full. Single-instance tail rate 4.565 -> 5.058 game frames/s (219 -> 198 ms
    per frame); single runs carry about +-12% scene noise, so that is not quoted as the gain.
  - Main thread at the EDGE list wait (`3E8388`): 56.4% -> 54.7% of samples (124 -> 108 ms per frame).
  - Timeline 600-620 s (`ftrace_report.py`): 88 -> 91 frames, median 208.9 -> 208.5 ms; main blocked at
    `3E8388` 129.1 -> 123.3 ms, RSX walker drain 151.8 -> 143.9 ms per frame, against ~190 ms of EDGE work per
    frame (0.96 x 198): the walker is not co-bounding yet, ~25% headroom.
  - So the next EDGE stage is still worth its job-time share: A880 (7.2% of job time in the x38 ranking).
- 2026-10-25: A880 boundary probe and block map (lab lift `work/spu_recomp_d935.fnpb_a880.c`:
  `gen_edge_native_lift.py`, then `spu_fnprobe_lift.py --no-reads --blocks` on A880 counting A880 B648 B650 B660
  5DE8; exe `build_runLabA8P`; cap_3 still 400/400; entry/exit states of all calls in `work/fnp_a880/`, game data).
  - **Calls:** 409 over the 400 jobs, in 383 jobs (357 once, 26 twice; the second call of a job rewrites the
    first one's output with the same bytes). `r3` = 0x107A0 in all. A880 reads three pointers from the
    job-context block (B648/B650/B660: words 0-2 of LS[r3 + 0x60]); when either of words 1-2 is 0 it returns
    at once by a tail call to B648 (241 calls, ~0 us). The other 168 take one path: A918 -> B648 (0) -> AD80 ->
    5DE8 (the output pointer) -> A928 -> loop `loc_A950` -> AD48 -> return. `r4` = element count, 9-558
    (median 205); the loop runs ceil(r4 / 8) times (2-70, median 26, 5287 in all) and writes all 8 elements of
    the last iteration, so 43 calls write past `r4` by design. Callees inside the region: none; indirect
    branches or calls: none (the only one in A880 is the early-exit tail call, outside the loop).
  - **Block map** (counts x lifted statements, 1.33M executed): the loop `A950` (247 statements) 98.1%; the
    entry block 1.0%; everything else under 0.3% each (5DE8 0.18%, B650/B660 0.09% each).
  - **Time** (`[fnpt]`, REPEAT=20, per-call minimum): 2009 us over the 168 loop calls (mean 12.0, median 9.95,
    max 26.4) = 7.2% of the 27.95 ms summed per-job minimum of that run; fit 0.371 us per A950 iteration +
    0.28 us (corr 0.9997).
  - **What the loop computes** (a reading of the lift and of the constants, identical in all 168 entries; to
    verify): per iteration 8 elements; element k reads quad A_k from the stream at r81 (word 2 of the block)
    and B_k from the one at r82 (word 1), and writes one quad to the output at r53 (+128 bytes per iteration
    for all three). With the shuffle control r47 = LS[0x10400] & 31 (words 1 2 0 0: "yzx"), t = B * A.yzx,
    c = t - A * B.yzx (fm, fnms: the cross product B x A, rotated), n = B.w * c.yzx with lane 3 cleared,
    L = (n.x^2 + n.y^2) + n.z^2 (fm, then two fa through the LS shuffle constants at 0xF840/0xF850: rotate by
    one and by two words), y = fi(L, frsqest(L)) and one Newton step r = (1 - L*y*y) * (y/2) + y (fm, fnms,
    fm, fma); out = (L > 0x0DA2425F (fcgt) ? n * r : n) with lane 3 = 0. That is normalize(B x A * B.w),
    plausibly a bitangent from normal and tangent (B.w the handedness). Inputs and output are separate buffers
    in cap_3 (0x29xxx-0x3xxxx); no degenerate element in any iteration but the last ones (30 of 42296
    elements have L <= eps, all in the overrun of the last iteration).
- 2026-10-25: the A880 loop native, in a lab build (`build_runLabA8N`; `build_runLabA8NP` with an unrouted A880
  probe wrapper, no block counters: lift `work/spu_recomp_d935.fnp_a880.c`).
  - **Pieces:** `port/gow_edge_native_a880.c` (an SPU source like the other three), behind `GOW_EDGE_A880`
    (read once): unset/0 = lifted, 1 = native packed v3 (`GOW_EDGE_A880_V2=1`: v2), 2 = native v1, 3 = check
    v3/v2, 4 = check v1 (all 128 GPRs and the whole LS at `loc_AD48`; `[edge-a880-check] N calls checked, M
    differ`, every 4096 calls and at exit). `GOW_EDGE_A880_FAULT=<n>[,<k>]` (native run k, default the first):
    1 = one LS bit in the first quad the last iteration stores, 2 = bit 0 of r73 after the loop.
    `port/gen_edge_a880_body.py <lift> <out.h>` writes `gow_edge_a880_body.h` (`GOW_A88_REGS/LOAD/STORE/BODY`)
    from A880's own copy of the loop (the alternate-entry functions A928, A950, AD48, AD4C and AD80 stay lifted)
    and checks the lift's shape: the back edge `if (r46 != 0) goto loc_A950` followed by `loc_AD48`, no other
    control flow, the registers read before written exactly r47-r53 and r81-r83, and the operation counts the
    packed path was written for. `gen_edge_native_lift.py` adds the fourth switch only where it is missing: in
    A880 (or its `_impl`) before `loc_0000A950:` (`goto loc_0000AD48`) and `check_end` after `loc_0000AD48:`
    (also reached by the skip in the A928 block when the count is not positive; not taken in cap_3). The port's
    CMakeLists builds the source and header like D640's; the default was the lifted loop (now 0 = lifted, unset =
    native: see below).
  - **Versions** (every value kept is the same op on the same operands as in the lift):
    - v1: the loop statement for statement in C locals (the generated macros), direct LS access.
    - packed: one iteration at a time, the 8 elements in the 8 lanes (the 16 input quads transposed into
      x/y/z/w vectors), computing only the lanes that reach memory: lanes 0-2 of the output, and lane 0 of the
      two sums (lane 3 and the other sum lanes are dropped by the lift's masks and splats). The helpers work
      lane by lane, so this is each lane's lifted result. Per iteration all loads come before the stores and
      the stores keep the lift's order, so the LS sees the same accesses (no overlap analysis). The last
      iteration runs the lifted body from the advanced pointers, so all 77 registers the loop writes (r80
      included) come out exactly. Entry checks, else v1: r47 and the 0xF840/0xF850 constants as above, no
      packed store reaching 0xF840/0xF850 (the lift reloads them every iteration), at least 2 iterations.
    - v3 (default packed) runs an iteration speculatively with one range check (as the other stages) and
      recomputes it with v2's ops before anything is stored if a lane fails; v2 checks every float op per lane
      with the 4-lane fallback. A self-test at mode init compares the 8-lane frsqest/fi/fcgt and the checked
      fm/fa/fnms/fma (zeros, exponent 0/255/254 and cancelling sums mixed in) with the 4-lane helpers: 0 of
      160000 quadwords differ each, 54681 8-lane ops through the 4-lane fallback.
    - Per cap_3 pass: 5119 of 5287 iterations packed (the rest is the lifted last iteration), 0 recomputed,
      0 ops redone by the 4-lane helpers, 0 entries to v1.
  - **Gates** (`build_runLabA8N`, `SPU_JOB_REPLAY=work/jobcap3`): unset, `=0`, `=1` (v3), `=1` with
    `GOW_EDGE_A880_V2=1` and `=2` (v1): each `[replay] 400/400 jobs identical`. Check mode: v3, v2 and v1 each
    `[edge-a880-check] 168 calls checked, 0 differ; 5287 iterations (8 elements each)`. Negative tests:
    - 1 (LS): run 0 (job_17_2, which calls A880 twice on the same buffers) job gate 400/400, the second call
      rewrites the byte; run 6 (`=1,6`, job_15_6, one call) `job_15_6.bin FAIL ... final LS differs first at
      0x2C78F: recorded 00, replay 01`, `399/400`; check mode `call 0: LS differs in 1 bytes, first at 0x2B78F
      (lifted 00 native 01)`, `168 calls checked, 1 differ`.
    - 2 (r73): job gate 400/400; check mode `call 0: r73 lifted 3F7FFF28 ... native 3F7FFF29 ...`, `1 differ`.
      As before, that the registers come out exactly rests on check mode.
  - **Timing** (pinned replay, REPEAT=50, sum of per-job minimum, ms; one exe, env alternated): lifted 28.332
    28.488 28.335 28.348 28.325 (median 28.34), v3 26.816 26.778 26.716 27.022 26.793 (median 26.79, -5.4%);
    v2 26.859 27.096; v1 28.190 28.069.
  - **The function alone** (`build_runLabA8NP`, `SPU_FN_PROBE=A880`, REPEAT=20, sum of the 409 per-call
    minimums, us): lifted 1987 2012 1988, v3 375 377 374 (5.3x), v2 567, v1 1693. Check-mode rdtsc (cold LS
    after the snapshot copies): ~400 vs ~1500 ticks per iteration.
  - **Not covered by any run:** the speculative recompute on real data (0 in cap_3; v2's ops are what it runs,
    gated above, and the fallback ops are covered by the self-test), the v1 fallbacks (one iteration, other
    controls, stores reaching 0xF840), the skip path to AD48, IRQ/RCHCNT inside a job, the game itself (no
    in-game check-mode run or in-game A/B yet: next, as for the other stages before their defaults flipped).
- 2026-10-25: A880 in game, check mode (x44: production exe with E550, 73E0 and D640 native, `GOW_EDGE_A880=3`,
  1100 s, no PM profiling): `[edge-a880-check] 1736704 calls checked, 0 differ; 55373180 iterations`; the paths
  cap_3 never took ran too: 126 speculative iterations recomputed with v2's ops, 126 8-lane ops redone by the
  4-lane helpers, 30 entries left to v1 (short). Check-mode rdtsc ~415 vs ~2035 ticks per iteration.
- 2026-10-25: A880 native in game, then the default.
  - **In-game A/B** (x45): the production exe (E550, 73E0 and D640 native, their defaults) in two copies,
    `GOW_EDGE_A880=1` vs unset (then lifted), swapped pairs, no PM profiling: window 4.219 vs 3.996 (+5.6%) and
    4.163 vs 3.965 (+5.0%) game frames/s. The replay predicted -5.4% of job time; measured over expected fps
    (see the D640 entry) is about 0.93 this time.
  - **Default:** `GOW_EDGE_A880` unset = native packed v3 now, `=0` the lifted loop. Gates on the lab build of
    that tree (`build_runLabA8D`): unset and `=0` both `[replay] 400/400 jobs identical`.
- 2026-10-25: after four stages (x47: production exe, all four native, one instance, 1100 s, PM profiling on,
  `PS3_FTRACE=600:20`), against x43 (three stages, same setup).
  - EDGE w15-17 busy: 96% of wall in both: still bound by EDGE job throughput.
  - ms/job (pm-prof median): 0.1183 -> 0.1122 (-5.2%), against the replay's -5.4%: A880 reached the game in
    full. Single-instance tail 5.058 -> 5.283 game frames/s (198 -> 189 ms per frame; single runs carry about
    +-12% scene noise).
  - Main thread at the EDGE list wait (`3E8388`): 54.7% -> 52.3% of samples (108 -> 99 ms per frame).
  - Timeline 600-620 s: 91 -> 98 frames, median 208.5 -> 200.0 ms; main blocked at `3E8388` 123.3 -> 98.2 ms,
    RSX walker drain 143.9 -> 142.5 ms per frame against ~181 ms of EDGE work (0.96 x 189): ~21% headroom left
    before the walker co-bounds.
- 2026-10-25: replay profile after four stages (x48: lab build of the production tree, all four native,
  `PS3_SAMPLE=1 PS3_SAMPLE_HOST=1`, REPEAT=1000, cap_3 400/400, sum of per-job minimum 26.53 ms; 11559 samples
  in the job window). Groups: d935 lifted 33.1%, overlay fragments 21.6%, runtime 12.8%, replay serving 12.3%
  (not game cost), native 73E0 7.0%, E550 6.4%, D640 4.0%, A880 1.4%. Top lifted d935 functions: F538 5.5%,
  DCC0 4.4%, 6AC8 3.7%, E550 outside its loop 2.9%, BBC8 2.0%. Overlay fragments are spread out: the largest
  function, `117D0` of overlay 0x719900, is ~4% summed over the several fragment lifts that each carry a copy;
  next `12250` (0x71c310) 2.5%, `11430` (0x7228c0) 2.2%. A native stage there would have to hook every lift
  holding the function, so the next stage is F538, then DCC0.
  - **Stop condition:** with ~181 ms of EDGE work per frame against a 142.5 ms RSX walker drain (x47), EDGE
    stages pay in full only while EDGE stays the longer one. F538 and DCC0 together would take EDGE to about
    165 ms; after that, re-measure the walker before another d935 stage.
- 2026-10-25: F538 boundary probe and block map (lab lift `work/spu_recomp_d935.fnpb_f538.c`:
  `gen_edge_native_lift.py`, then `spu_fnprobe_lift.py --no-reads --blocks` on F538 alone, which calls nothing;
  exe `build_runLabF5P`; `SPU_FN_PROBE=F538 SPU_JOB_REPLAY_REPEAT=20`, log `work/x49_fnpb_f538.log`; cap_3 still
  `[replay] 400/400 jobs identical`).
  - **Calls:** 461 over the 400 jobs, in 249 jobs (146 once, 5 twice, 87 three times, 11 four times). The
    lift calls it directly from five sites in 53B0 (copies in its alternate entries 5838, 5A78, 5C50); which of
    them cap_3 takes is not recorded. All 461 take one path: the entry block (the early return when r3 = 0 is
    never taken) -> loop `loc_F620` -> return. No callees and no indirect
    branch anywhere in F538; the loop's only control flow is its back edge `if (r37 != 0) goto loc_F620`. `r3` =
    quad count (44 distinct values, 0x50 in 160 calls, 0x38 in 116); the loop runs ceil(r3 / 8) + 1 times in all
    461 calls (3-73, median 11, 12164 in all): the extra iteration drains the pipeline. `r4` = input, `r5` =
    output (0x29000 in 249 calls); r4 = r5 (in place) in 450 of 461. `r6` = four matrix quads (0x10480 in 249
    calls, the stack 0x3F4F0 in 201, 0x104C0 in 11). 33 calls leave the LS unchanged (`wr=-`; output equal to the
    input, plausibly an identity matrix: not verified).
  - **Block map** (counts x lifted statements, 1.02M executed): the loop `F620` (82 statements) 97.4%, the entry
    block (57) 2.6%.
  - **Time** (`[fnpt]`, per-call minimum): 1340.6 us over the 461 calls (mean 2.91, median 1.2, max 7.9 us) =
    5.1% of the 26.12 ms summed per-job minimum of that run; fit 0.107 us per iteration + 0.08 us (corr 0.998).
  - **What the loop computes** (a reading of the lift; checked symbolically by the generator below, the
    meaning still to verify): per iteration 8 input quads v at r38 + 16k (r38 += 128). With four loop-invariant
    columns M0..M3 = r35 r36 r33 r34 (built in the entry block from the quads at r6 by selb/shufb with the LS
    controls at 0xFE70, 0xFF80, 0xFF60; not decoded), per quad: p1 = fm(v, M0), p2 = fma(rot_4 v, M1, p1), p3 =
    fma(rot_8 v, M2, p2), out = fma(rot_12 v, M3, p3), rot_n = rotqbyi by n bytes, so lane l = v[l]*M0[l] +
    v[l+1]*M1[l] + v[l+2]*M2[l] + v[l+3]*M3[l] (indices mod 4): a 4x4 matrix stored by diagonals, as E550's
    palette. Software pipelined: quads 0 and 1 are finished in the iteration that loads them (r20 r21), quads 2-7
    carry p3 (r9 r43 r41 r50 r49 r48) and rot_12 v (r53 r52 r51 r57 r56 r55) to the next iteration. Each iteration
    first loads its 8 quads, then stores the previous iteration's 8 results at r58 r54 r47 r46 r45 r44 r42 r40 +
    r39 (r39 += r32, r32 = 0 in the first iteration and 128 after it: the first iteration stores the prologue's
    zeros, the second overwrites them). The last iteration loads one group past the input. All four lanes of
    every result reach memory.
- 2026-10-25: the F538 loop native, in a lab build (`build_runLabF5N`; `build_runLabF5NP` with an unrouted F538
  probe wrapper, no block counters: lift `work/spu_recomp_d935.fnp_f538.c`).
  - **Pieces:** `port/gow_edge_native_f538.c` (an SPU source like the other four), behind `GOW_EDGE_F538`
    (read once): unset/0 = lifted (the default then; now 0 = lifted, unset = native: see below), 1 = native packed v3 (`GOW_EDGE_F538_V2=1`: v2), 2 =
    native v1, 3 = check v3/v2, 4 = check v1 (all 128 GPRs and the whole LS at the loop exit;
    `[edge-f538-check] N calls checked, M differ`, every 4096 calls and at exit). `GOW_EDGE_F538_FAULT=<n>[,<k>]`
    (native run k, default the first): 1 = one LS bit in the first quad the last iteration stores, 2 = bit 0 of
    r20 after the loop. `port/gen_edge_f538_body.py <lift> <out.h>` writes `gow_edge_f538_body.h`
    (`GOW_F53_REGS/LOAD/STORE/BODY`) from F538's own copy of the loop (the alternate-entry function F620 stays
    lifted) and checks the lift's shape: the back edge followed by the return (or the switch's exit label), no
    other control flow, the registers read before written exactly r9 r20 r21 r32-r58, every load before the
    first store, and, by evaluating the body symbolically, the eight stores (address, value, order) and the
    carried registers above. A changed fma operand or loop decrement makes it fail (tried on mutated copies).
    `gen_edge_native_lift.py` adds the fifth switch only where it is missing: in F538 (or its `_impl`) before
    `loc_0000F620:` (`goto gow_f620_exit`) and, after the back edge, the new label `gow_f620_exit:` with
    `check_end` (then F538's return). The port's CMakeLists builds the source and header like A880's.
  - **Versions** (every value is the same op on the same operands as in the lift):
    - v1: the loop statement for statement in C locals (the generated macros), direct LS access.
    - packed: one iteration at a time, two quads per vector (0|1, 2|3, 4|5, 6|7), the columns broadcast to
      both halves, rot_4/8/12 as one in-lane dword shuffle, 16 8-lane fm/fma per iteration, the lift's pipeline
      kept (the deferred fma of quads 2-7 runs at the top of the next iteration from the carried p3 and rot_12
      v). Per iteration all loads come before the stores and the stores keep the lift's order, so the LS sees
      the same accesses (in place included, no overlap analysis). The last iteration runs the lifted body from
      the carried registers, so all 65 registers the loop writes come out exactly. Fewer than 2 iterations (or
      more than 2048): v1.
    - v3 (default packed) runs an iteration speculatively with one range check (as the other stages; the
      carried registers and the loaded quads are range-checked as inputs, the columns once per entry, else the
      entry runs v2's ops) and recomputes it with v2's ops before anything is stored if a lane fails; v2 checks
      every fm/fma per lane with the 4-lane fallback. A self-test at mode init compares the checked 8-lane
      fm/fma (zeros, exponent 0/255/254 and cancelling sums mixed in) and the three rotations with
      `spu_fm`/`spu_fma`/`spu_rotqbyi`, then whole iterations (speculative when kept, and checked) with the
      helpers applied quad by quad: `checked fm/fma and rotqbyi 4/8/12 x8 0 of 200000 differ (62934 8-lane ops
      took the 4-lane fallback); whole iterations 0 of 5004 differ (1004 speculative kept)`.
    - Per cap_3 pass: 11703 of 12164 iterations packed (the rest is the lifted last iteration), 29 speculative
      iterations recomputed with v2's ops, 132 8-lane ops redone by the 4-lane helpers, 0 entries to v1, 0
      entries with non-plain columns. Unlike A880, cap_3 exercises the recompute and the fallback.
  - **Gates** (`build_runLabF5N`, `SPU_JOB_REPLAY=work/jobcap3`, logs `work/x49g_*.log`): unset, `=0`, `=1`
    (v3), `=1` with `GOW_EDGE_F538_V2=1` and `=2` (v1): each `[replay] 400/400 jobs identical`. Check mode (logs
    `work/x49c_*.log`): v3, v2 and v1 each `[edge-f538-check] 461 calls checked, 0 differ; 12164 iterations (8
    quads each)`. Negative tests (logs `work/x49n_*.log`):
    - 1 (LS): job gate `job_16_1.bin FAIL ... final LS differs first at 0x2B08F: recorded 00, replay 01`,
      `399/400`; check mode `call 0: LS differs in 1 bytes, first at 0x2B08F (lifted 00 native 01)`, `461 calls
      checked, 1 differ`. (Run 0 is job_16_1's only call, so no later call rewrites the byte.)
    - 2 (r20): job gate 400/400; check mode `call 0: r20 lifted 41680004 42214000 C2024018 3F800000 native
      41680005 ...`, `1 differ`. As before, that the registers come out exactly rests on check mode.
  - **Timing** (pinned replay, REPEAT=50, sum of per-job minimum, ms; one exe, env alternated; logs
    `work/x49t_*.log`): lifted 26.436 27.828 26.454 26.435 26.617 (median 26.45), v3 25.596 25.642 25.569 25.631
    26.755 (median 25.63, -3.1%; 27.828 and 26.755 are single outliers); v2 26.103 26.012; v1 26.275 26.280.
  - **The function alone** (`build_runLabF5NP`, `SPU_FN_PROBE=F538`, REPEAT=20, sum of the 461 per-call
    minimums, us; logs `work/x49p_*.log`): lifted 1349.8 1346.2 1350.4, v3 537.0 535.1 536.4 (2.5x), v2 916.6,
    v1 1149.2. The saving (~0.81 ms) matches the replay's (0.82 ms). Check-mode rdtsc (cold LS after the
    snapshot copies): ~300 vs ~580 ticks per iteration.
  - **Not covered by any run:** the v1 fallbacks (fewer than 2 iterations, more than 2048), entries with
    non-plain columns, the early return (r3 = 0; it does not reach the switch), IRQ/RCHCNT inside a job, the
    game itself (no in-game check-mode run or in-game A/B yet: next, as for the other stages before their
    defaults flipped). The meaning of the columns (the entry block's shuffles) is not verified.
- 2026-10-25: F538 in game, check mode (x50: production exe with E550, 73E0, D640 and A880 native,
  `GOW_EDGE_F538=3`, 1100 s, no PM profiling): `[edge-f538-check] 2392064 calls checked, 0 differ; 67385792
  iterations`; 213444 speculative iterations recomputed with v2's ops and 1150789 8-lane ops redone by the
  4-lane helpers on the way, 0 entries left to v1. Check-mode rdtsc ~280 vs ~653 ticks per iteration.
- 2026-10-25: F538 native in game, then the default.
  - **In-game A/B** (x51): the production exe (E550, 73E0, D640 and A880 native, their defaults) in two copies,
    `GOW_EDGE_F538=1` vs unset (then lifted), swapped pairs, no PM profiling: window 4.213 vs 4.120 (+2.3%) and
    4.209 vs 4.076 (+3.3%) game frames/s. The replay predicted -3.1% of job time (expected +3.2%).
  - **Default:** `GOW_EDGE_F538` unset = native packed v3 now, `=0` the lifted loop. Gates on the lab build of
    that tree (`build_runLabF5D`): unset and `=0` both `[replay] 400/400 jobs identical`.
- 2026-10-25: DCC0 boundary probe and block map (lab lift `work/spu_recomp_d935.fnpb_dcc0.c`:
  `gen_edge_native_lift.py`, then `spu_fnprobe_lift.py --no-reads --blocks` on DCC0 alone, which calls nothing:
  its only call is the tail call to B600 at its end, after the probe window; exe `build_runLabDCP`;
  `SPU_FN_PROBE=DCC0 SPU_JOB_REPLAY_REPEAT=20`, log `work/x53_fnpb_dcc0.log`; cap_3 still `[replay] 400/400 jobs
  identical`).
  - **Calls:** 400, one per job (entered from DCB0, whose `brsl r79` to the next instruction makes r79 a PIC
    base). Two paths: 286 through the loop at `loc_DE6C` (count <= 8), 114 through `loc_DEF8`/`loc_DFF0`
    (count > 8); `loc_E0C8`/`loc_E0CC` (count 0) never run. Then all 400: `E0E4` -> loop `E134` -> `E194` ->
    loop `E210` -> loop `E30C` -> loop `E3C4` -> tail call B600. Iterations per call (median, range): DE6C 6
    (3-83), DFF0 18 (5-33), E134 7 (2-82), E210 25 (2-222), E30C 6 (2-26), E3C4 35 (2-195). No callees and no
    indirect branch inside; one contiguous output (`wr=114C0+1910` in the first call).
  - **Block map** (counts x lifted statements, 2.59M executed; a block runs from its label to the next, so
    E210 includes the ~10 statements after its back edge and E30C the ~30 setup statements of E3C4): `E3C4`
    48.3%, `E210` 32.9%, `E30C` 5.0%, `DFF0` 4.2%, `E134` 3.7%, `DE6C` 3.2%, entry 1.7%, the rest under 0.6%
    each. The region `E194` .. the tail call is 87% of the statements.
  - **Time** (`[fnpt]`, per-call minimum): 1135.7 us over the 400 calls (min 0.30, median 2.15, mean 2.84, max
    13.6 us) = 4.4% of the 25.744 ms summed per-job minimum of that run. The split between the loops is not
    measured (a least-squares fit on the loop counts is degenerate: they all scale with the job).
  - **What the loops compute** (a reading of the lift): integer work only (shufb, cntb, fsmb, rotqby, selb,
    adds); no float op in DCC0. In `E3C4` the next iteration's four loads are at `mpyi(r12, 6)` + bases, with
    r12 the sum of the two bytes of a cntb of a mask of the quad this iteration loaded (`r66` rotated by r27),
    and the shuffle controls come from LS tables at 0xFC00/0xFD00 (r79 = 0x4000, + 0xBC00/0xBD00) indexed by
    the same quad; in `E210` the next address adds 2 x (cntb of the loaded quad & 15). So each iteration's
    addresses depend on the previous iteration's loads: a serial variable-length decode (plausibly EDGE index
    decompression: not verified).
  - **Decision:** not staged. The region is clean (no callee, no indirect branch), but the stage template
    (8 lanes from two quads or from 8 elements per iteration, the float range check) has nothing to work on:
    the iterations cannot be packed and there is no float op. Only a v1 applies; v1 alone gave ~15% on A880
    (1987 -> 1693 us) and F538 (1349 -> 1149 us), i.e. ~0.17 ms, ~0.7% of job time here. A later DCC0 stage would
    need its own design (v1 with direct byte-shuffle/popcount helpers). Next candidate: 6AC8.
- 2026-10-25: 6AC8 boundary probe and block map (lab lift `work/spu_recomp_d935.fnpb_6ac8.c`: as for DCC0, on
  6AC8 alone; exe `build_runLab6AP`; `SPU_FN_PROBE=6AC8 SPU_JOB_REPLAY_REPEAT=20`, log `work/x53_fnpb_6ac8.log`;
  cap_3 still `[replay] 400/400 jobs identical`, sum of per-job minimum 25.260 ms).
  - **Calls:** 118 over the 400 jobs, one per job. 117 take one path: entry block -> outer loop `loc_6BE0`
    with the inner loop `loc_6DE0` in every outer iteration -> `loc_6F40` -> exit `loc_6F98` (return). The
    entry block's skip to 6F98 (count not positive) and the outer body's skip of the inner loop (`r85 == 0`)
    are never taken. 1 call (r9 = r10 at entry) takes `loc_6FC0`, which ends in a tail call to 5D90; it never
    reaches the loops. No callee and no indirect branch inside the loop nest (5D90 is the only call in 6AC8).
    `r4` = 0x29000 and `r8` = 0x11000 in all 118 calls.
  - **Iterations:** outer 1867 in all (1-65 per call, median 8) = ceil(r7 / 8), 8 vertices per iteration;
    inner 3614 (2-195, median 15) = r10 - r9 per outer iteration (the sphere count): 1 in 12 calls, 2 in 99, 3 in
    6.
  - **Block map** (counts x lifted statements, 592k executed): `6DE0` (88 statements) 53.7%, `6BE0` (123)
    38.8%, `6F40` (19) 6.0%, entry 1.3%, exit 0.2%: the loop nest is 98.5%.
  - **Time** (`[fnpt]`, per-call minimum): 922.8 us over the 118 calls (mean 7.8, median 4.1, max 42.5 us) =
    3.65% of 25.260 ms; fit 0.195 us per outer + 0.147 us per inner iteration + 0.25 us (corr 0.999).
  - **What the nest computes** (a reading of the lift; the dataflow is checked by the generator below, the
    meaning still to verify): per outer iteration 8 vertices. Vertex k has the offset idx_k = word 3 of the
    quad at r67 + 16k, the position P_k = LS[r48 + idx_k] and the output O_k = LS[r34 + idx_k]. The positions
    are transposed by selb/shufb (masks r54 r56, controls r81-r83 from LS 0xF870/0xF880/0xF890: whole-word
    picks in the one capture read) into X Y Z of vertices 0-3 (r44 r42 r38) and 4-7 (r43 r40 r37). For each
    sphere j of r9 .. r10 - 1 (S_j = LS[r51 + 16j], V_j = LS[r50 + 16j]): D = |S_j.xyz - P|^2 (fs, fm, two fma),
    d = D * rsqrt(D) (frsqest, fi, one Newton step: fm, fnms, fm, fma, fm), x = d * S_j.w, t = 1 - min(x, 1)
    (fcgt, selb, fs), s = t^2 (3 - 2t) (fm, fnms, fm), w = s^2 * (r53 V_j).w, and each vertex's accumulator
    (starting at r79: 0 or 0.5 per a flag) += w * r53 V_j (fma). Then out_k = O_k * r49 + acc_k (fma; each lane of r49
    1.0 or 0, from flag bits in r12) is stored at r34 + idx_k, vertex 0 to 7 in order (read-modify-write by index: a duplicate
    index means the later store wins). The inner loop is software pipelined: one iteration applies sphere j's
    weight, finishes sphere j+1's distance and computes sphere j+2's squared distance, loading two spheres past
    the last.
- 2026-10-25: the 6AC8 loop nest native, in a lab build (`build_runLabDC6N`; `build_runLabDC6NP` with an
  unrouted 6AC8 probe wrapper, no block counters: lift `work/spu_recomp_d935.fnp_6ac8.c`).
  - **Pieces:** `port/gow_edge_native_6ac8.c` (an SPU source like the other five), behind `GOW_EDGE_6AC8` (read
    once): unset/0 = lifted (the default for now), 1 = native packed v3 (`GOW_EDGE_6AC8_V2=1`: v2), 2 = native v1,
    3 = check v3/v2, 4 = check v1 (all 128 GPRs and the whole LS at `loc_6F98`; `[edge-6ac8-check] N calls
    checked, M differ`, every 4096 calls and at exit). `GOW_EDGE_6AC8_FAULT=<n>[,<k>]` (native run k, default the
    first): 1 = one LS bit in the first quad the last outer iteration stores (vertex 0's), 2 = bit 0 of r33 after
    the nest. `port/gen_edge_6ac8_body.py <lift> <out.h>` writes `gow_edge_6ac8_body.h` (`GOW_6A_REGS/LOAD/STORE`
    and the blocks `GOW_6A_A/PRE/INNER/TAIL`) from 6AC8's own copy of the nest (the alternate-entry functions
    6BE0, 6DE0, 6F40 and 6F98 stay lifted) and checks the lift's shape: the three tests and back edges where
    expected, `loc_6F98` after the outer back edge, no other control flow, no jump into the nest from outside,
    loads only in A and INNER and stores only in TAIL, the registers read before written exactly the loop
    invariants (r29 r34 r35 r39 r41 r47-r54 r56 r64-r66 r77-r85) plus r67/r68, and, by evaluating each block
    symbolically: A's gathers, transposes, the first two spheres' squared distances and distance and the eight
    accumulators' start value, PRE's copies, INNER's whole carried state and TAIL's eight stores (address,
    value, order). A changed fma operand, swapped fnms constants, a different selb mask in the transpose, a
    changed outer step or an added store each make it fail (tried on mutated copies). `gen_edge_native_lift.py`
    adds the sixth switch only where it is missing: in 6AC8 (or its `_impl`) before `loc_00006BE0:` (`goto
    loc_00006F98`) and `check_end` after `loc_00006F98:` (also reached by the entry block's skip and from the
    6FC0 path; check_end ignores those). The port's CMakeLists builds the source and header like F538's, so the
    production exe carries the switch too (unset = lifted).
  - **Versions** (every value kept is the same op on the same operands as in the lift):
    - v1: the nest statement for statement in C locals (the generated macros), direct LS access.
    - packed: one outer iteration at a time. The index and position loads and the transposes are the lift's
      (the 4-lane selb/shufb helpers with the live masks and controls, so any control works); then X Y Z hold
      vertices 0-3 in the low half and 4-7 in the high half. Per sphere, one 8-lane op per lifted lo/hi pair for
      the distance and the weight, and the accumulators pair vertex k with k+4 under the sphere's scaled vector
      in both halves (the weight lane splatted per half): 4 fma per sphere for the lift's 8. Only the spheres that
      reach the stores are computed (the lift computes two more, loading them past the table; those values only
      reach registers, and the last outer iteration is lifted anyway). Per outer iteration all loads come before
      the stores and the stores keep the lift's order (vertex 0 to 7), so the LS sees the same stores (the packed
      path loads a subset of the lift's addresses). The last outer iteration runs the lifted blocks from r67/r68
      advanced, so all 58 registers the nest writes come out exactly. Entry checks, else v1: at least 2 outer
      iterations, the splat controls r47/r41/r39/r35 = words 0/1/2/3, a sphere count of 1..4096 when the inner
      loop runs.
    - v3 (default packed) runs each sphere step speculatively with one range check and recomputes that step
      with v2's ops if a lane fails (the other stages check once per iteration; here the step is the unit, as
      the accumulators carry from step to step and the steps are most of the work). The ops on a step's outside
      inputs (fs of the positions, fm of the sphere's vector) and the final fma with the output quads always run
      checked (the per-lane rule with the 4-lane fallback), their results range-checked as inputs: cap_3's
      positions and output quads carry non-plain lanes now and then (an earlier build that range-checked the
      positions as inputs ran 777 of 1750 outer iterations on the checked ops). v2 checks every float op per
      lane with the 4-lane fallback. A self-test at mode init compares the 8-lane frsqest/fi/fcgt and the
      checked fm/fs/fnms/fma (zeros, exponent 0/255/254, cancelling and equal operands mixed in) with the 4-lane
      helpers, then whole outer iterations (8 vertices, 0-3 spheres; the capture's transpose controls, other
      whole-word picks or random bytes; speculative and checked) against v1, i.e. the lifted statements, on a
      scratch context: `frsqest/fi/fcgt x8 0 of 160000 differ; checked fm/fs/fnms/fma x8 0 of 160000 differ
      (85110 8-lane ops took the 4-lane fallback); whole outer iterations against v1 0 of 3940 differ (2434 of
      6000 sphere steps kept speculative, 566 recomputed)`.
    - Per cap_3 pass: 1750 of 1867 outer iterations and 3386 of 3614 sphere steps packed (the rest is the lifted
      last iteration and 3 calls with one outer iteration, left to v1), 0 speculative steps recomputed, 967 8-lane
      ops redone by the 4-lane helpers (in the checked input and output ops), 0 entries with other controls, 0
      with non-plain constants.
  - **Gates** (`build_runLabDC6N`, `SPU_JOB_REPLAY=work/jobcap3`, logs `work/x53g_*.log`): unset, `=0`, `=1`
    (v3), `=1` with `GOW_EDGE_6AC8_V2=1` and `=2` (v1): each `[replay] 400/400 jobs identical`. Check mode
    (logs `work/x53c_*.log`): v3, v2 and v1 each `[edge-6ac8-check] 117 calls checked, 0 differ; 1867 outer
    iterations (8 vertices each), 3614 sphere steps`. Negative tests (logs `work/x53n_*.log`):
    - 1 (LS): job gate `job_15_6.bin FAIL ... final LS differs first at 0x2C0DF: recorded DF, replay DE`,
      `399/400`; check mode `call 0: LS differs in 1 bytes, first at 0x2C0DF (lifted DF native DE)`, `117 calls
      checked, 1 differ`.
    - 2 (r33): job gate 400/400; check mode `call 0: r33 lifted 3F004618 3EF04E6F 3F2D9D2D 410B82DF native
      3F004619 ...`, `1 differ`. As before, that the registers come out exactly rests on check mode.
  - **Timing** (pinned replay, REPEAT=50, sum of per-job minimum, ms; one exe, env alternated; logs
    `work/x53t_*.log`): lifted 25.924 26.052 29.339 25.927 26.028 (median 26.03; 29.339 a single outlier), v3
    25.466 25.491 26.632 25.316 25.345 (median 25.47, -2.2%); v2 25.549 25.882; v1 26.847 25.707.
  - **The function alone** (`build_runLabDC6NP`, `SPU_FN_PROBE=6AC8`, REPEAT=20, sum of the 118 per-call
    minimums, us; logs `work/x53p_*.log`): lifted 922.3 914.9 922.3, v3 372.1 391.6 460.6 (2.0-2.5x; the spread
    is unexplained), v2 482.3, v1 757.5. The saving (~0.53 ms) matches the replay's (0.56 ms).
  - **Not covered by any run:** the speculative recompute on real data (0 steps in cap_3; it runs v2's ops,
    gated above, and the self-test recomputes 566 steps against v1), the inner-loop skip (`r85 == 0`: the inner
    loop ran in all 117 calls; the self-test covers 0 spheres against v1), the fallbacks to v1 on controls or
    count and the non-plain-constants path (0 entries each), the 6FC0 path (it never enters the nest), IRQ/RCHCNT
    inside a job, the game itself (no in-game check-mode run or in-game A/B yet). Not verified: the meaning of
    the nest (S, V, O and the flags), and that the transpose controls at LS 0xF870..0xF890 are always the ones
    read from one capture (the packed path does not depend on them).
- 2026-10-25: 6AC8 in game, check mode (x54: production exe with the other five stages native,
  `GOW_EDGE_6AC8=3`, 1100 s, no PM profiling): `[edge-6ac8-check] 1355776 calls checked, 0 differ; 20558077
  outer iterations (8 vertices each), 37802963 sphere steps`; on the way 1050 speculative sphere steps
  recomputed, 11800663 8-lane ops redone by the 4-lane helpers, 32769 entries left to v1 (short). Check-mode
  rdtsc ~1200 vs ~2709 ticks per outer iteration.
