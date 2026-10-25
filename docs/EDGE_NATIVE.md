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
- **Job end:** a dispatch to the link the job was entered with, `spu_halt` on the context, or the next job start on
  the same thread. Other dispatches into PM code (pc < 0x4000) are JobApi calls and stay in the capture. On EDGE the
  job never returns to its link, so a capture ends at the module's exit to the kernel (see progress log).
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
| `E550` | 13.4% | ? | |
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
