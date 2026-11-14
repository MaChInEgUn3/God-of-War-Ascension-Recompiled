# Status (updated 2026-11-12)

## Where it stands
* **Runs:** boot, intro movies, main menu with text and music, and the first level on the Vulkan backend: the attack
  tutorial, the prison and the corridor fight, driven by a scripted pad. No hang in 25-minute runs.
* **Speed, corridor fight, 6-core CPU + RTX 3080:** about 16-18 game frames/s on a quiet host (x415, x417-x418) and
  about 14-15 with other programs busy (x416, x419). A frame takes ~65-70 ms. Not playable yet. The optional
  profile-guided build adds about 5% (18.1-18.7 against 17.5-17.7 on a quiet host, x446). Those figures predate
  the wider lane default of x447-x450 (+6% to +12% on the in-run lever). Whole runs with it read 17.9 on a quiet
  host (x450w, second round) and 15.2-16.6 when the host was less quiet (x451).
* **Played by a person (2026-11-10):** New Game with a DualSense, 11.5 minutes: the opening, the tutorials and on
  into a courtyard arena fight, where it drops to 7-9 game frames/s. How far the game gets beyond that is open.
* **Open bugs:** graphics bugs reported in that session, of which two causes are fixed (2026-11-11, below: blown-out
  bloom and decals, and rectangles in the shadows) and the rest is not triaged; texture garbage on some meshes in a
  minority of scripted runs (2 of 30 when last counted); one unexplained freeze in ~30 scripted runs (x430b, below).
  Fixed on 2026-11-10: a freeze a minute into New Game (instructions the lift had left out, below).

## Where the time goes (2026-11-08, x429-x443)
* The frame is the main thread's own work in series with its waits on the SPU job lists; about two thirds of the
  main thread's samples are those waits.
* No single part paces the frame. Slowed by 20% one at a time, the EDGE lists cost about a quarter of full pacing, the
  General lists a third, the RSX walker under a third, and no single list is outside the noise.
* The main thread's own work has no hot spot: outside its waits, no function has 1% of its samples (x440).
* So a lever on one component is capped by that component's share. The ones sized since x429 all came out under what
  the in-game A/B resolves (3-10% between arms) and are parked with their numbers in the log below.

## Profile-guided build (2026-11-09, x444-x446)
* Recipe ([BUILDING.md](BUILDING.md), `scripts\pgo.bat`): an instrumented build, one 14-minute scripted run of the
  first level and its corridor fight (the instrumented exe runs at about 7 game frames/s), then a rebuild with that
  profile. It covers the lifted PPU, the lifted SPU and the runtime together (56,693 functions with counts).
* Corridor fight, game frames/s over 240-450 s, 480 s runs in the order standard / PGO / PGO / standard:

  | Round | Host load at each start (cores) | Standard | PGO | PGO | Standard | PGO against standard |
  |---|---|---|---|---|---|---|
  | x445 | 1.7 / 1.1 / 0.5 / 1.4 | 16.44 | 17.71 | 18.43 | 16.23 | +10.6% |
  | x446 (waited for a quiet host) | 0.5 / 0.5 / 0.6 / 0.5 | 17.73 | 18.70 | 18.13 | 17.46 | +4.7% |

* The profile-guided exe was ahead in both pairs of both rounds. The quiet round is the cleaner figure, about +5%:
  in x445 the standard runs started under more load than the PGO runs, so part of its +10% may be the host.
* Limits. The profile comes from one scene: code that scene never ran is optimized as cold, so other levels may gain
  less or lose (not measured). Lifted code that changes after the profile loses its counts until the training run
  is repeated.

### Rebuilt on the 2026-11-12 source, trained on two scenes (p1-p2)
* The x444 profile predated the lane default, the graphics fixes and the 129 EDGE placements lifted on 2026-11-12,
  so the profile was made again: two training runs of the instrumented exe (the corridor fight, 14 minutes; the
  still view at the corridor's end from a later save, 10 minutes), merged into one profile.
* Freshly booted host, 390 s runs in the order standard / PGO / PGO / standard, flips/s over 240-360 s:

  | Round | Standard | PGO | PGO | Standard |
  |---|---|---|---|---|
  | p1a-p1d | 15.53 | 17.35 | 18.14 | 17.35 |
  | p1e-p1h | 18.04 | 18.08 | 18.12 | 16.52 |

  Still view, one run each, 240-330 s: PGO 14.97, standard 14.38 (+4%).
* Reading. The PGO runs sit at 17.4-18.1 (mean 17.9); the standard runs spread over 15.5-18.0 (mean 16.9). That
  spread is as large as the difference and the best standard run equals the PGO runs, so the +6% between the means
  is not a proven figure; p1a was also the first run after the reboot. What repeats in every run is the cost of an
  EDGE job run: 73-77 us with the profile against 76-87 us without in the corridor (per run 73, 74, 77, 75
  against 79, 80, 76, 87), and 78 against 84 us in the still view, about -7%. No PGO run was behind the standard
  run next to it. The earlier figure of about +5% (x446) still describes it.
* Cost of the build: with a profile (and with the instrumentation) one compiler process takes 3.4 GB or more on
  the large lifted files. On a 32 GB machine 4 jobs (instrumented) and 2 jobs (profile use) were killed for
  memory; 2 and 1 jobs worked, and the profile-use build then takes about an hour.
* Kept as an optional last build step; the experiments below and the next A/Bs use the standard build.

## More SPURS lanes (2026-11-09/10, x447-x450)
* The four RenJobSet lists (wids 9-12) now run on up to three kernel threads, as the EDGE lists and General6 /
  BatchJobList already did: port default `SPURS_LANES=15,16,17=3;3,7=3;9,10,11,12=3`. They ran one lane wide at the
  end of the frame (wid 11 alone is ~13 ms of it, x402).
* Measured with the in-run lever (`GOW_AB=lanes`, `GOW_AB_LANES`, 5 s windows from 300 s, corridor fight, 1.6-3.8
  cores of other load on the host). Game frames/s with the lanes on against off:

  | Lanes switched | On | Off | Difference | Block median ms/frame (lever faster in) |
  |---|---|---|---|---|
  | 9-12 on 3 (x448ra) | 18.43 | 16.42 | +12.2% | -2.69 (7/9) |
  | 9-12 on 3 (x449rb) | 15.60 | 14.27 | +9.3% | -5.82 (5/9) |
  | 9-12 on 3 (x449rc) | 15.92 | 14.99 | +6.2% | -4.33 (7/9) |
  | 9-12 on 6 (x449sa) | 13.36 | 12.36 | +8.1% | -1.20 (5/9) |
  | 9-12 on 2 (x449ta) | 15.46 | 14.48 | +6.8% | +0.99 (4/9) |
  | 3 and 7 on 6 instead of 3 (x448ga) | 17.51 | 17.54 | -0.2% | +2.35 (3/8) |
  | 2 and 14 on 6, 13 on 3 (x448oa) | 16.76 | 15.91 | +5.3% | +1.55 (3/9) |
  | All of the above together (x447a) | 18.29 | 17.11 | +6.9% | -0.58 (6/9) |
  | All of the above together (x447b) | 18.46 | 17.42 | +6.0% | -0.02 (5/9) |

* Noise floor, recomputed 2026-11-10 from all 21 runs with no lever (x445, x446, x450w, x451): the on/off totals
  read -4.1% to +6.3% (mean +1.0%, standard deviation 2.5); the block median reads -2.16 to +5.98 ms (mean +1.94,
  standard deviation 2.3, so it is not centred on zero); the lever counts as faster in 2 to 6 blocks of 9. The first
  eight runs alone had given -1.1% to +2.7%, which was too narrow.
* Against that floor only the RenJobSet lists on three lanes stand out: three runs out of three, block medians
  (-2.69, -5.82, -4.33 ms) all below the lowest null value, two of the three totals above the highest. The other
  rows are inside it, including the "all together" rows.
* Frames with the lanes on look like the default's; no `[pm-hang]` in any run.
* Before (x405, x406): wid 11 alone on two lanes showed nothing, and 11 with ModelServer about +2%. The four lists
  together had not been tried.
* As a whole-run setting (x450w, 2026-11-10): 480 s runs in the order old default / new / new / old, game frames/s
  over 240-450 s: 15.47 / 15.85 / 14.56 / 13.90, so +3.5% for the new default (+2.5% and +4.7% in the two adjacent
  pairs). The batch started after the host had been under 1.3 cores of other load for three minutes, but the load
  did not stay there (1.7 / 0.9 / 4.1 / 1.1 cores at the four starts, 5 after the last) and every run is slower than
  the quiet rounds above. Four whole runs do not resolve a difference of this size: in the eight runs of x451 below,
  the same exe ranged over 8-9% from run to run. The in-run lever figures in the table are the evidence for the
  lanes; x450w only shows the whole-run result does not contradict them.
* A second round of the same four runs (2026-11-10 06:52-07:26, load at the starts 2.4 / 1.9 / 0.9 / 0.7 cores):
  13.17 / 17.94 / 17.94 / 16.85. The first run is discarded: other work of ours ran on the host inside its
  measurement window (its EDGE module run took 81.8 us against 71-72 in the other three). The last three ran on a
  quiet host: 17.94 and 17.94 for the new default against 16.85 for the old, +6.5%, in line with the in-run
  figures. One pair, so still within what whole runs resolve.
* An earlier attempt (x448w, all the lanes above against the old default) ran while another program used 3-5 cores
  and settles nothing: 15.88 / 13.32 for the default against 15.08 / 15.30.
* Also tried: the walker's park spin and idle nap cut short at the next vblank tick (`GCM_PARK_TICK_CAP=1`, lever
  `parkcap`, x450p): +1.0% and +3.1% on the totals, block medians +3.20 and +0.02 ms. Inside the noise floor; the
  default stays off.

## Link-time optimization (2026-11-10, x451)
* ThinLTO alone, no profile: every object compiled to bitcode (`/clang:-flto=thin`), optimized and code-generated by
  lld-link over the lifted PPU, the lifted SPU and the runtime together. Same source as the standard exe. The build
  took 47 minutes at idle priority on a busy host (the standard full build takes about 20) and the exe came out
  0.3% smaller.
* It runs the same: the first level to the corridor fight, frames look the same, no `[pm-hang]` in nine runs.
* Corridor fight, game frames/s over 240-450 s, 480 s runs, two rounds in opposite orders, each started after three
  minutes under 1.3 cores of other load:

  | Round | Order | Host load at each start (cores) | Run 1 | Run 2 | Run 3 | Run 4 |
  |---|---|---|---|---|---|---|
  | x451l | standard / LTO / LTO / standard | 2.1 / 0.8 / 1.9 / 1.2 | 16.45 | 16.96 | 16.90 | 15.17 |
  | x451m | LTO / standard / standard / LTO | 3.5 / 0.8 / 0.9 / 1.2 | 15.50 | 16.48 | 16.63 | 16.59 |

* Pooled over the eight runs: 16.49 with ThinLTO against 16.18 standard, +1.9%. ThinLTO was ahead in both adjacent
  pairs of the first round and in neither of the second. The same exe ranged 15.17-16.63 (standard) and 15.50-16.96
  (ThinLTO) from run to run, so the difference is inside what these runs resolve. The time of one EDGE module run and
  the walker's time per draw did not move with it.
* Parked: no measurable gain for a build more than twice as long. Not tried: ThinLTO together with the profile
  (cross-module inlining is driven by call counts when a profile is present), which would be the form to retest.
* Method notes from these rounds. A four-run whole-run comparison on this host resolves about 5% at best; smaller
  effects need the in-run lever or many more runs. The first run of a batch tends to start under more load than the
  rest, and reading logs while a run is inside its measurement window slows that run (run 4 of x451l). A quick check
  of a run: the median EDGE module run is 71-72 us on a quiet host and 75-84 us when something else was busy.

## Main thread after the lanes, and HighestPrio6 (2026-11-10, x452-x453)
* Sampled profile of the current exe (x452, 300 s, all threads) against the one before the RenJobSet lanes (x440),
  corridor fight only (samples from 240 s). One run a side, on different days, 4.7 against 1.4 cores of other load
  at the start: direction only.

  | Main thread | x440 (RenJobSet one lane) | x452 (three lanes) |
  |---|---|---|
  | Its share of all samples | 18.7% | 14.0% |
  | `0x416338` job-slot scan | 31.5% of the thread | 14.3% |
  | `0x466E38` wait on the HighestPrio6 counters | 24.3% | 31.0% |
  | `0x416610` | 8.9% | 5.5% |

  The scan at `0x416338` dropped to a third of its samples (2458 to 814); the HighestPrio6 wait did not move (1900
  to 1763) and is now the largest item. The walker and the SPU threads look the same as before.
* So HighestPrio6 (wid 2), which runs one lane wide, was tried on more lanes with the in-run lever on top of the
  current default (x453, 0.4-2.9 cores of other load):

  | Lanes switched | On | Off | Difference | Block median ms/frame (lever faster in) |
  |---|---|---|---|---|
  | 2 on 3 (x453ha) | 16.75 | 15.99 | +4.8% | -0.23 (5/9) |
  | 2 on 3 (x453hb) | 17.15 | 16.83 | +1.9% | +2.41 (4/9) |
  | 2 on 6 (x453sa) | 17.57 | 16.95 | +3.7% | +1.82 (3/9) |
  | 2 and ModelServer (5) on 3 (x453ma) | 17.16 | 16.71 | +2.7% | +0.01 (4/9) |

* All four are inside the noise floor above: not resolved, a few percent at most, default unchanged. ModelServer
  added nothing on top.
* Why (x454, x455: 20 s trace windows of the corridor fight on a diagnostic exe that marks the main thread's wait
  loops; frame 59 ms). The wait at `0x466E38` is 10.3 ms of the frame, ahead of the sleep at `0x3E8388` (6.4),
  the scans called from `0x416654` and `0x416A44` (4.6 and 4.4) and the RSX `ref` throttle at `0x375A88` (4.2). But it
  is not a wait for HighestPrio6: that list runs 1.35 ms a frame, and 89% of the wait has none of its runs inside.
  With wid 2 on three lanes the list did spread over two or three SPUs and the wait stayed at 10.2 ms.
* What it tracks is RenderJobs (wid 13), one lane wide, busy 24.3 ms a frame in 40 runs: it is running during 87% of
  the wait, and the end of one of its runs is the nearest event to the end of the wait in 58% of the waits (the
  short HighestPrio6 waits make up most of the rest). `0x466E38` is the generic "wait for this job list" routine.
* RenderJobs on three or six lanes (x455c, x455d) removes that wait, 10.3 to 0.8 ms a frame, and the frame does not
  get shorter: 58.6 ms. The time moves to the other waits (`0x3E8388` 8.9 ms, `0x375A88` 6.5, a poll at `0x3E8910`
  3.6). So the main thread was waiting there for something that is ready no sooner; the frame is paced elsewhere,
  the RSX side being the first suspect. This matches the in-run levers that had wid 13 on three lanes (inside the
  noise). Default unchanged.

## New Game froze a minute into the first level: instructions the lift left out (2026-11-10, x456-x457)
* First run by a person (New Game, DualSense): the game froze 345 s in, in the courtyard after the attack tutorial.
  In the log, 40 s before: a query returned a null object, the main thread walked a tree from it (322 indirect
  calls to addresses that are not functions; the scripted corridor runs have none), then stalled waiting for a job
  while the RSX walker sat on a self-jump in the command buffer.
* Cause: the lifted PPU code was made by an older lifter that did not know `stvlx` (63 sites) or `vlogefp` (8) and
  emitted them as no-ops. In `func_002E9270` four `stvlx` write vectors to the stack before a call; with them
  missing the callee read whatever was there and the result's object pointer came out null. On the console that
  read would fault, so the object exists there. The scripted route never takes this branch.
* Fix: `scripts/patch_lift.py` fills those 71 instructions in with what the current lifter emits (a no-op on a
  current lift). The other `TODO` lines in the lift are data inside code.
* Check. The player's inputs were recorded (`PAD_RECORD`) and replayed (`PAD_PLAY`, see DEBUGGING.md). Before the
  fix the null object came back in 3 of 5 replays, at 302-304 s, each time the routine was reached; after it, the
  one replay that reached the routine (3050 calls) had no null read. Then the same person played 11.5 minutes on
  the fixed exe, past that spot: no null read, no bad call, no freeze.
* A fresh lift with the current lifter and the command in BUILDING.md is not a drop-in replacement: it came out as
  29,299 functions in 35 files, 2.1 GB of C++, against 16,336 functions in 5 files, 190 MB. Not understood yet, so
  the port keeps the old lift plus the patch.

## Past the corridor: the arena fight is much slower (2026-11-10)
* That 11.5-minute session went on into a courtyard arena fight. Game frames per second from the log's heartbeat:
  12-15 on the way there, then 9.5, 8.1 and 6.6 over three consecutive minutes of the fight (the corridor fight
  runs at 16-18). Nothing was profiled there yet. The save from that session is kept as a second scene.
* The player also reports many small graphics bugs along the way; the frame dumps of the session show light and
  dark rectangles on floors. Two causes were found the next day (below).

## Graphics: two causes found in the played session's bugs (2026-11-11, g1-g2)
Method: a per-pass capture (`RSX_ENGINE_PASS_DUMP`, DEBUGGING.md) of one frame in two scenes that hold still, the
scripted corridor scene and the scene the played session's save opens on. Both fixes are in the toolkit patch.

* **Blend constant never applied.** The Vulkan backend mapped the four constant blend factors (`0x8001`-`0x8004`:
  constant colour, constant alpha and their complements) to ONE, and nothing read `SET_BLEND_COLOR`. The game uses
  them in three places: its bloom up-chain (`8003/8001`, constants `FF000000` and `4C3D3D3D`, so "source x 1 +
  destination x 0" and "source x 0.30 + destination x 0.24"), the bloom add (`8003/1`) and floor decals (`8001/8004`,
  37 draws in the captured corridor frame). All of them went in as a plain add. Now the constant is passed before
  every draw and carried as Vulkan dynamic state; `VK_BLEND_CONST=0` gives the old mapping.
  * Measured: over 10 fight frames of the corridor scene, pixels at full brightness were 0.90% and 1.09% before
    (two runs) and 0.40% after. The runs are separate and their frames do not line up, so this shows the direction
    only.
  * Not verified: the decals themselves (the frame captured after the fix had none on screen), and the half-float
    target branch (the game's blended targets are 8-bit).
* **A texture smaller than the target at its address.** The game renders into the top-left part of a target and
  then samples that part with a texture that declares only its size. The engine bound the whole target, so the
  lookup covered all of it. `RSX_SUBRECT_TEX=0` turns the fix off.
  * Shadows: the four shadow cascades share one depth target (1402x1408 in the captured frame) and each uses a
    part of it (754x172, 348x514, 52x54 and 124x140). With the whole target bound, the third and fourth cascade
    changed 0 pixels of the shadow mask. The mask is filled in 80x90 screen tiles, so the missing cascades showed
    as rectangles with stepped edges on floors: the "light and dark rectangles" of the played session. With the
    declared size passed to the shadow lookup they change 3307 and 31902 pixels, and the rectangles are gone from
    the mask and from the scene pass of that frame. Judged on one still scene; the fight has not been looked at
    again by a person.
  * Colour: once a frame the game reads 320x360 of a 640x360 target (a half-size pass that feeds the alpha of the
    shadow mask). That read now takes the texture's own size; the pass output is then the left half alone
    instead of both halves squeezed side by side. First done through guest memory, then with a copy on the GPU
    (below, with its cost).
* The save of the played session opens at the end of the corridor before the arena, not in the fight. Two things
  seen there with a scripted pad (g2-g3):
  * That still view runs at 11-12 frames/s with no input at all, so it is a slow scene that repeats exactly.
    Profiled against the corridor scene in back-to-back runs of one exe, the host in use by other programs both
    times (g5b, g5c; a comparison, not figures to quote):

    | | still view at the corridor end | corridor scene |
    |---|---|---|
    | frames/s | 11.3-11.7 | 14.3-16.6 |
    | draws per frame | about 2400 | about 2050-2130 |
    | RSX walker, CPU per draw end | 27.4-27.8 us | 22.5-25.4 us |
    | RSX walker, CPU per frame | about 66 ms of 87 | about 50 ms of 65 |
    | main thread, largest share of samples | 26% in the frame-start wait, 10% in the end-of-frame call | 19% waiting for the render jobs, 13% in the RSX `ref` throttle |

    The frame-start wait is the game's own limit on frames in flight: `func_003E799C` (called at `0x3E8384`)
    sleeps 30 us at a time until the count of the frame slot two back is zero. That is the "sleep at `0x3E8388`"
    left open on 2026-11-10. The walker's split per draw is the same as before (vertex fetch about 10 us,
    textures 3-8, backend 5-6, pipeline lookup 2, index build 1.6).
  * **What paces the still view: the EDGE geometry lists, not the RSX walker** (in-run causal probes, one run
    each, 16 windows on and 15 off of 5 s, g8a-g8e). A stage that fully paces the frame loses 16.7% to a 20%
    slow-down, or the whole added wait:

    | probe | frames/s on | off | change |
    |---|---|---|---|
    | RSX walker passes +20% (`rslow`) | 11.50 | 11.57 | -0.6% |
    | main thread +10 ms at each flip request (`mslow`) | 11.52 | 11.53 | -0.1% |
    | SPU workload 3 +20% | 11.27 | 11.32 | -0.4% |
    | SPU workload 8 +20% | 11.25 | 11.51 | -2.2% |
    | SPU workloads 15, 16, 17 (EDGE geometry) +20% | 9.91 | 11.32 | -12.4% |

    The first reading of this view, from the walker's CPU share and the main thread's wait, was that the walker
    paced it. The probe says it has slack: it catches up with the main thread about 113 times a frame and its
    drains take 53 ms of a 72 ms frame (a `PS3_FTRACE` of the view, g6a). The GPU waits inside them are 5.5 ms a
    frame (3.4 for a batch slot, 2.1 in synchronous submits, of which 2.0 creating textures and 0.4 a readback).
    Each of the three EDGE workloads is busy 77-80% of the time here (55-69% in the corridor scene), about 570
    job runs a frame each. The probes that spin also add host CPU, so part of each drop is contention, not
    pacing.
  * **The corridor scene, same probes** (g9a-g9f, one run each, 17 windows on and 17 off, 16.2-16.7 frames/s
    off; its last probes, 2026-11-08, predated the wider lanes):

    | probe | frames/s on | off | change |
    |---|---|---|---|
    | SPU workloads 15, 16, 17 (EDGE geometry) +20% | 14.51 | 16.39 | -11.5% |
    | RSX walker passes +20% | 14.95 | 16.19 | -7.7% |
    | SPU workload 13 (render jobs) +20% | 15.72 | 16.38 | -4.0% |
    | main thread +10 ms at each flip request | 15.80 | 16.19 | -2.4% |
    | SPU workload 8 +20% | 16.19 | 16.32 | -0.8% |
    | SPU workload 3 +20% | 16.92 | 16.68 | +1.4% |

    There the frame is shared: the EDGE lists carry about two thirds of a full pacing share, the walker a bit
    under half, the render jobs a quarter. In both scenes the EDGE geometry lists are the largest single share,
    and in the still view the only one.
  * A new texture costs about 3.4 ms on the walker (hash, create, upload and a synchronous submit). In this
    view they fall from about 4 a frame after the spawn to under 1; in the corridor scene to almost none.
    Recording the first upload in the draw batch, as the in-place re-upload already is, would remove the wait.
  * The colour sub-rectangle read (above) cost 3-4% here while it went through guest memory: 10.4-11.1 frames/s
    against 11.3-11.7 with `RSX_SUBRECT_TEX=0`, 0.9-1.0 s per 30 s in the store against 0.05 s. About 900 draws a
    frame sample that texture in this view, not one. Fixed the same evening with a copy on the GPU (backend
    `color_part`: the part is copied into a texture of the declared size that stays with the surface and is
    refilled only after the surface was drawn to; on Vulkan the copy joins the draw batch): 11.0-11.8 frames/s and
    0.05 s (g7b), and the pass that reads the part shows the same image as before (g7a).
    `RSX_SUBRECT_TEX=2` keeps the guest-memory path.
  * **Open bug: the arena cutscene starts and stops on its first shot after Continue.** A few seconds' walk from
    the spawn point (the played session's own stick path does it) the picture dissolves to a close shot of Kratos
    at the arena entrance, and stays there for the rest of the run (9 runs of 9, up to 3 minutes): three camera
    framings cycle, the stick and the attack buttons do nothing, START still opens and closes the items menu, no
    other button has an effect. In the played session, reached by playing rather than by Continue, the same
    cutscene ran about 40 s through its other shots and the fight began.
    * Looked at (g6a, a `PS3_FTRACE` of 90 s around the trigger): file reads go on for about 20 s after the
      trigger and then fall to a trickle, so nothing is stuck reading; no PPU thread that shows in the trace blocks
      for good; the two `snd_stream_parsing_thread`s never return from their wait in those 90 s, before or after
      the trigger. There is no trace of the played session to compare against, so that last point is a lead, not
      a finding. The game prints no batch-job message after the trigger, and the log shows no error.
    * Not it: `PS3_MUTE` (it only silences the device).
    * Ruled out on 2026-11-12 (12 runs of 12 now):
      * The test's own input. The nine runs above had the scripted X presses of the run script during the shot;
        the person pressed nothing during the cutscene. Without them, and with the person's recorded inputs from
        the checkpoint to the end of the session, the picture stays on one framing for 340 s at 30 frames/s (r5a).
      * The port's VRAM release guard, which refuses four releases in the seconds after the trigger: the same
        with `GOW_RELEASE_GUARD=0` (r5b).
      * The sound-stream threads. They show no wake in 70 s of a cutscene that does run either (the first
        fight's intro in a replay, r6a), so their silence says nothing.
      * A later change to the port: the same on the exe the session was played with (r7a).
      * Continue as such: from the checkpoint before the first fight, with no input, that fight's intro cutscene
        runs through its shots to the attack prompt (r8a). So it is this checkpoint, or this cutscene reached
        from it. Parked until a person plays through it again with the profile capture on.
    * Seen in both the played session and the repro, so not a difference: the game's
      `BATCHJOB: AddJob: waiting for room in job list 3 for job 56...`, six `UnmapIoAddress` and two
      `MapMainMemory` (0x800000 and 0x700000). In the played session the maps came 13 s after the trigger, in the
      repro 2 s after it.
* Replaying the played session's recorded inputs from New Game went out of step in the opening fight (the game
  ran at a different speed than when it was recorded), so that is not a scripted way into the arena. (2026-11-12:
  the replay now re-aligns at autosaves and passes that fight; it stops in the corridor fight. Section below.)
* The arena fight draws about as much as the corridor fight (about 2000 draws per frame in both, from the played
  session's log), while its draws per second fall from about 40 thousand to 15-20 thousand. So the slowdown is
  not the amount drawn, and it is not something that builds up: the rate recovers later in the same session.

## Past the corridor the geometry jobs ran partly in the interpreter (2026-11-12, h1-h2)
The probes above named the EDGE geometry workloads; their profile said why. In the still view at the corridor end
a quarter of their busy time was the SPU interpreter (`[pm-prof]` `i24-25%`), in the corridor scene none. The
EDGE jobs load small code fragments into local store, each placement is lifted on its own, and the 569 lifted
so far came from the menu, the first level and its corridor. The scenes after it use placements nobody had seen.

* Capture (`SPU_LSA_GETLOG`, `SPU_INTERP_MISS=1`; no measurable cost, a 2 MB log): 12 s interpreted in every 20 s
  in the still view, summed over the lanes, and about 31 s in every 20 s in the first shot of the arena cutscene,
  all of it at fragment placements. 129 new placements lifted: 698 now.
* Before and after, one run each, the host in use (the first three rows do not depend on host load):

  | still view at the corridor end | before | after |
  |---|---|---|
  | interpreter share of EDGE busy time | 24-25% | 0% |
  | EDGE job run | 113-117 us | 86-90 us |
  | interpreted, per 20 s | about 12000 ms | about 230 ms |
  | frames/s | 11.3-11.7 | 13.2-14.0 |

  First shot of the arena cutscene: interpreted time about 31000 -> 240 ms per 20 s, about 21 -> 23-26 frames/s.
  Corridor scene: nothing interpreted before or after, its native stages and reloc blobs still in use, EDGE job
  run 89 us against 86-88 us for the previous exe run right after it (the same, within run-to-run spread),
  13.8-15.7 frames/s. Frames of the still view from both exes show the same geometry.
* Two things the lift ran into, now in BUILDING.md: placements must be lifted from the `[lsa-get]` lines alone
  first (with the `[interp-miss]` lines in the same pass, new lifts of the natively patched blobs got an entry at
  the patched loop's head and the port's generator refused them); and the fragment image ids now reach 997, so
  the reloc blobs moved from 900-903 to 2000-2003.
* The same probes on the still view with the new exe (h3a-h3e, one run each, 16 windows on and 15 off,
  13.2-13.8 frames/s off). The frame is now shared, with the EDGE lists still the largest share:

  | probe | frames/s on | off | change | before the lift |
  |---|---|---|---|---|
  | SPU workloads 15, 16, 17 (EDGE geometry) +20% | 12.02 | 13.59 | -11.5% | -12.4% |
  | main thread +10 ms at each flip request | 12.62 | 13.36 | -5.5% | -0.1% |
  | SPU workload 3 +20% | 13.05 | 13.73 | -5.0% | -0.4% |
  | RSX walker passes +20% | 12.72 | 13.20 | -3.6% | -0.6% |
  | SPU workload 8 +20% | 13.61 | 13.80 | -1.4% | -2.2% |

  Workload 3 is busy 97-101% of the time there now (82% before).
* More lanes do not help there: six for the EDGE workloads +0.0%, six for workloads 3 and 7 -0.1% (in-run
  `lanes` lever, h4a/h4b). A trace of the view (g6a, before the lift) shows why: each EDGE list runs its jobs
  one after another for most of the frame (59 ms busy of 74.5 each), and never more than three EDGE lanes are
  busy at once, one per list (three at once 42.7 ms of the frame, two 20.6, one 8.0, none 3.3). The main
  thread's frame-start wait falls entirely inside that (14.4 of its 14.6 ms with an EDGE lane busy). So the
  frame there is about one EDGE list's serial time, some 570-600 job runs, and what is left to gain on these
  lists is the cost of a job run (88 us now), not their parallelism.
* Whether the arena fight (6.6-9.5 frames/s when played) has the same cause is not known: nothing scripted reaches
  it. A played session with the capture on would say, and would give its placements.

## Hitches: every run translated every shader again (2026-11-12, k1)
* Frame times of the played session of 2026-11-10, from the `[flip]` lines (one every 10 flips, so each value is
  a 10-frame mean): in the corridor part the median frame is 53 ms, but 18% of the time goes to stretches whose
  mean is over twice that, up to 223-442 ms a frame. In the arena fight there are no such stretches: the frame is
  evenly slow, median 97 ms and later 156 ms. Two different complaints, "stutters" and "slow".
* `ENG_SLOW_MS=40` on a replay of the opening names the stutter: of 6.0 s in draw ends over 40 ms, 5.5 s is
  pipeline creation, 35-130 ms each, at the first use of a material. Almost all of that is glslang translating
  the shader to SPIR-V on the walker thread. The toolkit can keep the translations on disk
  (`PS3RECOMP_SPIRV_CACHE=<directory>`), and the port never gave it a directory.

  | opening, 4 minutes | cache cold | cache warm |
  |---|---|---|
  | pipeline creation in slow draw ends | 4.7 s | 0.1 s |
  | draw ends over 40 ms | 64 | 20 |
  | worst 10-frame mean | 147 ms | 78 ms |

  The port now sets it to `%LOCALAPPDATA%\gow-ascension-recomp\spirv_cache` (383 shaders and 16 MB for the
  opening); with that default and a filled directory the same replay shows 0.0 s of pipeline creation.

  **Correction (2026-11-12, evening).** The `[eng-slow]` log stops after 64 lines. The "6.0 s", "5.5 s" and the
  cold column above are therefore the first 64 slow draw ends of the run, not the run; the warm column (20 lines)
  is complete. Whole-run figures are in the next section.
* What is left: the first time through a scene still translates (the cache fills as one plays, and it is
  derived from the game, so it is not something to ship); the slow draw ends that remain are texture work
  (0.6 s) and the backend (0.3 s); the driver's own pipeline cache is not kept between runs yet.

### The first sight of a shader: 38 ms was the optimizer, not the translation (2026-11-12, t1, u1-u2)
* Correction to the above: the cache only worked for the exe that filled it. Its files carried the compile time
  of the translator's source file, so every rebuild, and the profile-guided exe next to the standard one, started
  from an empty cache. The header now carries a revision string, bumped by hand when the translation changes.
* `PS3RECOMP_SPIRV_TIME=1` splits a translation. Opening, cache empty, 400 shaders: parse 2.9 ms, link and io map
  0.3 ms, SPIR-V generation 34.6 ms. Generation for HLSL input runs a whole SPIRV-Tools optimization list.
* What those passes are for is making the SPIR-V legal. Offline on the same 400 modules as generated with no
  pass (SPIRV-Tools validator, Vulkan 1.0):

  | Passes after generation | ms per shader | valid |
  |---|---|---|
  | none | 0 | 400 of 400 |
  | the validator alone | 4.1 | - |
  | SPIRV-Tools' legalization list | 24.5 | 400 of 400 |
  | SPIRV-Tools' performance list | 38-48 | 400 of 400 |

* So the default is now: no passes, then the validator; a shader that fails gets the legalization list, and
  glslang's own list if it is still not valid. `PS3RECOMP_SPIRV_OPT=full` is the old behaviour.

  | | full list | validate only |
  |---|---|---|
  | translation, opening, cache empty | 37.8 ms | 9.3 ms |
  | translation, corridor fight, cache empty | 42.3 ms | 10.4 ms |
  | flips/s 240-360 s, corridor, cache warm (val / full / full / val) | 16.94, 15.51 | 17.12, 15.81 |

  750 translations, none failed validation, no shader module or pipeline failure.
* **What that is worth on a first pass: 16% less time in slow draw ends, not a factor of four.** The driver
  compiles what it is given, and unoptimized SPIR-V costs it more. The fair comparison needs the driver's own
  shader cache cold as well, and totals that count every draw end (the two things the first version of this
  section got wrong: it compared against modules the driver had already seen, and it summed the `[eng-slow]`
  log, which stops after 64 lines). `PS3RECOMP_SPIRV_SALT=<n>` changes a header word of each module so that the
  driver has not seen it, and `ENG_PHASE_STATS` now prints the total of all draw ends over `ENG_SLOW_MS`.
  Opening replay, 240 s, translation cache empty, modules salted (m2):

  | passes | translations | draw ends over 10 ms | of that: pipeline | texture | backend |
  |---|---|---|---|---|---|
  | glslang's full list | 11.6 s (33.0 ms each) | 679, 33.9 s | 14.0 s | 9.2 s | 10.6 s |
  | none, validator (default) | 3.3 s (9.4 ms each) | 736, 28.6 s | 4.7 s | 11.4 s | 12.5 s |
  | inline + local load/store removal | 7.7 s (19.4 ms each) | 689, 29.5 s | 8.6 s | 9.8 s | 11.1 s |
  | the same + dead code elimination | 7.7 s (19.2 ms each) | 688, 29.5 s | 8.6 s | 10.1 s | 10.8 s |

  Without passes the pipeline phase is 9.3 s shorter and the texture and backend phases, where the driver's
  work shows, 4.1 s longer. The two short lists land in between and are no better in total. A second scene (corridor fight, m3): 46.9 s with the full list against 31.5 s validate-only in draw ends over 10 ms, but the host had 5.0 cores of other load during the first and 2.2 during the second, so only the direction counts. One run per mode;
  only this GPU and driver.
* Whether a run meets a shader for the first time matters less than this table suggests once the directory is
  filled: the translation cache now survives rebuilds and is shared by the standard and the profile-guided exe.
* Texture phases over 10 ms are 9-11 s of those 240 s in every mode, so they are not a product of the
  unoptimized shaders, though those add about 2 s. Where that time goes is open.
  * Tried and taken out again (x1): the texture create's layout transition is a submission of its own that the
    walker waits for, behind everything the queue holds. Recording it into the open draw batch instead did not
    shorten the texture stalls over 40 ms (opening 1.1 s against 1.6 s, corridor 2.1 s against 1.9 s; these
    counts are under 64, so complete, but a 40 ms cut sits inside a cluster of 41-48 ms stalls and is a noisy
    measure). Not shown to help; not shown to be useless either.
  * `VK_SLOW_MS=10` on the opening (x2a): slow `vkQueueSubmit` calls of 13-89 ms each, 13-24 per run and under
    1 s in total in every mode of the table above, so they are a small part of it.
  * Next: split the texture phase (guest read and conversion, create, upload) in the `[eng-stats]` line's
    `tex:` buckets, which already exist, over a whole run. Done, below.

### The texture cache misses 23 times a second, on textures it already has (2026-11-12, y1-y3)
* The `tex:` buckets of `[eng-stats]` over whole corridor runs (390 s): 8,400-9,200 cache misses a run at
  2-2.5 ms each (16-22 s), about 4,000 re-uploads of changed textures (8 s) and 3-4 s of hashing. The opening has
  12,378 misses in 240 s (27 s). That is 7-11% of the walker's time, all the time, not a first-use cost.
* It is not a cache that is too small. The engine keeps 1024 textures and the cache is full, but with 4096
  entries the misses are the same (9,226): it fills and goes on evicting.
* `ENG_TEX_MISS_LOG=1` prints each missed key. 8,021 misses had 7,983 different offsets: all in VRAM, DXT1 and
  DXT5, 128 to 1024 px with full mip chains, 2.6 GB of texture data. With the content hash added: 9,895 misses
  held 794 different contents, and 92% of the misses carried content an earlier miss had (87% within the last
  1024 misses; the same content comes back after a median of 161 misses). The title keeps placing the same
  textures at new addresses, and the engine, which knows a texture by its address, uploads each one again.
* Built (`RSX_TEX_REUSE`, z1): on a miss of a DXT texture, a cached entry with the same
  content and descriptor that was not checked in this frame gets the new address and keeps its image. Corridor,
  390 s, off / on / on / off after a discarded run:

  | | off | on | on | off |
  |---|---|---|---|---|
  | misses that took a cached image | 0 | 7,975 (84%) | 10,263 (85%) | 0 |
  | time in the miss bucket | 19.0 s | 6.6 s | 9.3 s | 22.7 s |
  | texture data decoded on misses | 3,197 MB | 630 MB | 637 MB | 3,638 MB |
  | texture phase, all draw ends | 50.8 s | 41.4 s | 43.6 s | 56.7 s |
  | flips/s, 240-360 s | 15.30 | 17.28 | 16.02 | 15.72 |

  The frame rate is not resolved by these runs (the discarded off-run read 17.76). **On by default since the
  evening of 2026-11-12** (`RSX_TEX_REUSE=0` turns it off), after two image checks (y5): the opening replay with
  it on (86% of its misses reused) shows menus, cutscenes and the first fight with the right textures on a sheet
  of 48 frames, and the still view with it on differs from a run with it off no more than two runs with it off
  differ from each other (mean absolute difference 26.3 and 12.3 against 24.5; that view is not still enough at
  a given present for the comparison to be sharp, so this says "nothing gross", not "identical"). The engine's
  unit test was not run with it. Scenes no script reaches (the arena) have not been seen with it. Not built: the same lookup where an address's content has changed (about 4,000 re-uploads and 8-9 s a
  run, now as large as what is left of the miss bucket).
  * Counted since (c1a, a line `texture re-uploads:` in `[eng-stats]`): of 2,993 re-uploads in a corridor run,
    238 (8%) brought content another cached texture held. A changed address mostly gets content the cache has
    not seen, so that lookup is not worth building.
  * The batched texture create, measured again with the reuse on (b1): time in the miss bucket 6.6 s with the
    create as its own submission against 7.4 s and 9.6 s batched (the fourth run was cut short). No gain shown;
    it stays off.
* Open: why the title re-places textures at all. With nothing moving (the still view at the corridor's end, no
  input) there are still 808 misses in the last two minutes, 233 different contents, one of them 83 times. The
  title's own VRAM cache does not settle in this port. That cause is worth more than the fix above, and the same
  save is the one whose cutscene does not start. Not looked at yet.
  * First count (v1a, `GOW_RELEASE_STATS=30`, a counter at the entry of the title's release function): in the
    still view the title releases 31-46 objects every 30 s, 20-27 MB of top mips, with nothing moving, and the
    port's release guard refuses almost none of them (4 in five minutes; its log lines stop after 8, so earlier
    counts of "4 skips" were capped). So the guard is not what keeps the cache from settling: the title is
    evicting and re-streaming top mips all the time, as if it were short of VRAM. Next: how much VRAM and
    streaming budget the title believes it has in this port, against a console's. Checked: the port reports 256 MB of local memory to the title (a console reports 249 MB), so it is not the reported size. What the title's streaming measures itself against is not known yet.
  * Found (v2-v4, 2026-11-14). Reporting the console's exact local size changes nothing (38-42 releases per
    30 s). The title's per-frame compactor (0x372B70) sets a need-memory flag while (A + B) / total > 0.75, with
    A = `*0x96DDDC`, B = `*0x96E164`, total = `*0x96DDE0` and the 0.75 a constant at 0x372B68; with the flag
    set it releases every resident texture that is not marked in use (side-entry flags 0x8000 and 0x2000 set,
    0x4000 clear). In the still view the pool reads A 119.3 MB + B 0 of 130.2 MB = 0.917, constantly, so the
    flag is always set. A sample of 40 releases: one 640 KB texture is released and streamed back every 13
    frames or so (27 of the 40, its countdown not expired), another of 1.25 MB every 93 frames; the rest are
    ordinary expiries. The releases do not lower A: what is freed is streamed back.
  * Open: whether 119 MB is what this scene holds on a console too (then the churn is the title's own design
    at this fill and only its cost matters), or whether the port keeps textures marked in use that a console
    would have let go (then A is inflated and the fix is there). Not decided; the threshold itself is not
    something to patch.
  * The corridor fight is fuller still (v5a): A 130.2 MB of 130.2 MB, with 19.5-20.6 MB more waiting (B), a
    ratio of 1.15, and only 2-11 releases per 30 s because nothing is releasable. In both scenes the title has
    more in use than its pool holds.
  * Ruled out (q1b, `GCM_REPORT_STATS=30`): that the port makes the title draw more than a console by
    answering every occlusion query "visible". The title issues about 7,700 type-1 report queries a second,
    all to main memory, but never enables the pixel count and never clears a report: they are the
    synchronisation it uses reports for, not occlusion queries.
  * **Correction (2026-11-14, n1a): this is a port bug, not the title's design.** The side-entry flag 0x4000 is
    set in one place only (0x370B48, reached through a pointer, when a load has brought in all of a texture's
    top mips): it means "load complete". The sampled releases are of textures whose load HAD finished
    (descriptor +0x20 equal to the evictable size) and whose flag was still clear. With a hook at 0x370B48 the
    still view shows that function ran fewer than 201 times in the run (the hook's log only starts there, and stayed empty): every one of 40 sampled releases had
    no load report. So the finished loads of the textures that churn are not flagged, the compactor takes them for unfinished ones while the
    pool is over its threshold, releases them, and they load again. An earlier version of this entry called
    that "loads still in progress" and guessed it might be the title's own behaviour; the sample already
    contradicted it.
  * Second correction (n3d): an earlier wording here said that function is never called at all. It is: a log of the calls into the title's table of handlers (a static struct at 0x997770, where 0x370B48 sits in three slots) shows it reached from two call sites of a dispatcher around 0x3C9A80. What holds is narrower: it runs rarely, and not for the roughly 1,400 re-loads a run of the textures that churn.
  * Next: what should call 0x370B48 for those. Its descriptor is at 0x813580 and no lifted function builds that address
    directly, so it is registered through the TOC; find who registers it, with which interface, and why the
    port never calls back. A guard variant (g2a) changed nothing and was removed.
* On the side (`VK_TEX_CREATE_BATCHED=1`, off by default): with a new texture's layout transition recorded
  into the open draw batch, a miss cost 1.96 and 1.93 ms against 2.50 and 2.15 ms, with no change in frame rate
  that the runs resolve. The earlier "did not shorten the stalls" above looked only at draw ends over 40 ms.
  Frames were not compared; to be measured again after the content reuse.

## A scripted way to the arena fight: not yet (2026-11-12, r1-r4)
* The arena fight is the slowest scene a person has played and no script reaches it: the scripted corridor run
  never leaves the corridor (30 minutes, x412), and Continue from the checkpoint before the arena stops on the
  cutscene's first shot (above).
* Replaying the person's 11.5-minute recording from New Game now gets through the opening and the first fight on
  time: the replay re-aligns at each autosave and no longer skips short presses
  ([DEBUGGING.md](DEBUGGING.md), "Record and replay"). Autosaves when played: 41, 158, 260, 326, 354 s. Replayed
  (r4a): 41, 148, 246 s, and then none.
* It stops in the corridor fight: enemies and gates need a reaction, and the same inputs replayed blind leave
  the character against a wall. The replay from the corridor's own checkpoint (r1a) ends the same way.
* So the fight is still measured only when a person plays it.

## Where an EDGE job run's time is, corridor fight (2026-11-12, s1a)
* Host samples of the threads while they run workloads 15-17 (`PS3_SAMPLE=1 PS3_SAMPLE_HOST=1
  PS3_SAMPLE_WIDS=15,16,17`, 48,755 samples from 240 s on, resolved against the link map):

  | Share | What |
  |---|---|
  | 55.9% | the hand-made native stages (largest: 11.5%, 6.9%, 6.0%, 5.3%, 4.5%, 3.9%, 3.4%) |
  | 25.2% | the SPU runtime: indirect branches and lookup 5.3%, channel writes 4.2%, MFC transfers 3.5%, draining calls 2.6%, the per-GET overlay bookkeeping 2.0%, reservation checks 2.0%, channel reads 1.8% |
  | 13.2% | lifted job-module code, flat (no function above 1.1%) |
  | 1.4% | lifted code fragments |
  | 4.3% | other |

* So lifted code is about 15% of an EDGE run in this scene. Two cheap ideas for it were tried on one fragment
  file and dropped on the generated assembly alone: `restrict` on the context pointer changes nothing (6598 to
  6597 instruction lines), and keeping the registers in locals for the length of a block makes it longer (6598
  to 7191; blocks are short and use each register about once). A lifter that merges blocks would be working on
  that 15%.
* What is left is spread: the native stages are already 8-wide with a checked fast path, and no runtime item
  is above 5%. In this scene the next percent comes from several small things, not one.

## Next
* The first run through a scene: translate shaders off the walker thread. (Keeping the driver's pipeline cache
  is dropped: with the translated shaders on disk, pipeline creation is already 0.1 s for the opening.)
* A played session with the fragment capture on: the arena fight's placements, and whether the interpreter is
  what slows it.
* The arena fight: where its time goes (it is 2-3 times slower than the scene everything above was measured in),
  and a scripted way to reach it.
* A person looking at the fight again with the two graphics fixes, and whatever graphics bugs remain.
* The cutscene at the arena entrance that does not start after Continue (above).
* The EDGE geometry workloads (15, 16, 17) are the largest pacing share in both probed scenes (probes above).
  Their interpreted fragments are lifted for the scenes a script reaches; what is left is what their job runs
  cost and whether more of them can run side by side. Probe the still view again, workload 3 first.
* A second thread for the RSX walker was the plan until the probe showed the walker has slack in the still
  view. In the corridor scene it carries a bit under half a share, so it comes after the EDGE lists.
* (Answered in part 2026-11-11: the sleep at `0x3E8388` is the game's limit on frames in flight. What clears
  the count it waits on is not established; in the still view slowing the walker does not lengthen the frame
  and slowing the EDGE workloads does.)
* ThinLTO together with the profile-guided build has not been tried; alone it gave nothing measurable (above).

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

## Log
The first blockers in the order they fell (M0.12-M0.21), then the notes of each milestone and experiment, not in
strict date order. Statements here describe the build of their date.

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
Per-job dispatch cost, first pass (profile with the new default, 3396 samples from 250 s): on the EDGE kernel
thread ~21% is dispatch (`spu_indirect_branch` 7, `spu_lookup` 7, `spu_task_launch_check` 4, `spu_drain_call` 3);
on the PPU main thread `ppu_trace_enter` is 5% (the lift instruments all 21k guest functions). Toolkit now caches
`spu_lookup` results per thread (misses too, invalidated by new registrations) and rejects in `ppu_trace_enter`
before any other work; the port defaults `PPU_TRACE_ADDRS=0` (without it, 4000 `[FN]` lines per boot). Measured
neutral: new 1.18-1.20 vs old 1.14-1.19 on one instance, 1.12-1.18 vs 1.15-1.17 swapped (+1% pooled, inside the
noise), 12 runs, 0 corrupted. Kept as clean-ups. Instance A runs ~2% faster than B, so A/B pairs must swap.

Update (M0.52, 2026-10-22): **the frame waits on the GPU, one draw at a time.** The chain, measured in the
level:
- The main thread waits ~620 ms per game frame for the WWS job lists and works ~500 (`frame_wait.py`).
- The three kernel threads are only 217% busy of 300 (`[pm-prof]`); the EDGE workloads (wids 15-17, ~1670 jobs
  per game frame, ~0.58 ms each) spend 15-27% of their time reading the decrementer (`[pm-prof-ch]`: ch8).
- That read is a backoff: `jobmod_d93503647dd8` function 0xB700 polls a label in main memory by DMA (the RSX
  ring-space label) and, while there is no room, spins `rdch $ch8` 2400 times (`SPU_DECPC`). Making the read cheap
  (TSC instead of QPC plus divides) only made it poll more often; the share went up, the wait stayed.
- The ring frees as the FIFO walker passes its semaphores, and a `PS3_FTRACE` timeline (the Vulkan backend now
  logs each synchronous submit as a GPU_WAIT span) shows the walker thread blocked on the GPU 66% of the time:
  `vk_eng_draw` submits and waits on a fence for **every draw** (54,759 draws in 20 s, 168 us each, 46% of the
  thread), `vk_eng_depth_snapshot` another 11% (375 x 5.8 ms).
This also explains why the re-run dispatch gave +65% EDGE job entries per minute and 0% frames: more SPU work
cannot help while the ring drains at one fenced draw at a time. Next: batch draws into one command buffer
(per-draw vertex/index/uniform ring slices and descriptor sets, flush before any other backend operation).

Update (M0.53, 2026-10-22): **draw batching, on by default.** `VK_DRAW_BATCH=1` records engine draws into one
command buffer: each draw takes slices of three ring buffers (vertices 64 MB, indices 16 MB, constants 32 MB) and a
descriptor set from a pool, with a full memory barrier between draws in place of hazard tracking. Up to
`VK_DRAW_BATCH_MAX` (default 512) draws go per submission. `vk_begin`, every object destroy, the present and the
engine's own submit point flush first, so all other backend work still sees everything before it complete.
- Timeline (20 s in the level): GPU wait 13.1 s → 8.4 s; the sync-submit wait per flip 228 → 97 ms; flip median
  264 → 190 ms.
- A/B, same exe, instances swapped halfway. In the two pairs with matched level entries (instance B, the slower
  one): 1.593 vs 1.141 and 2.089 vs 1.852 game frames/s. The other two pairs entered at different times, so they
  compare different scenes, but they had the same order. Absolute numbers vary with the scene: 2.09 with batching
  off in one run is scene, not code.
- 8 runs: no hang, no `invalid size`, no BRANCH-TO-0. The vram-guard trips are the same "release skipped" kind with
  batching on or off. Some frame dumps show texture garbage, with batching on or off (see the open bug below).
Update (M0.54, 2026-10-22): **the rest of the per-operation waits go into the batch, and the GPU stops being
the limit.** Depth snapshots copy through a persistent device-local buffer that is recorded into the open batch.
Texture uploads stage through a 64 MB ring and record a buffer-to-image copy. Colour and depth/stencil clears
record too.
- GPU wait per 20 s in the level: 8.4 s with draws batched, 5.6 s with snapshots and uploads batched, 2.9 s with
  clears batched. What is left is readbacks (~0.6k x 2.8 ms) and the present, which need the result.
- Snapshots and uploads, A/B against draw batching alone (all 8 runs entered at 240 s): 2.097 vs 1.974 mean game
  frames/s (+6%). They won 3 of 4 pairs; one pair was reversed.
- Clears, A/B on the three pairs with matched entries: 1.394 vs 1.372, 1.620 vs 1.596 and 1.619 vs 1.602
  (+1.5%).
- Halving the GPU wait again moved frames by ~1.5%, so the frame is now bound elsewhere (the CPU side: SPU jobs and
  the main thread). No hang, `invalid size` or BRANCH-TO-0 in 16 runs. The texture garbage below appears at the
  same rate with and without these changes.
Update (M0.55, 2026-10-22): **a steadier metric, and the batching results re-scored.**
- The old window [entry+60, entry+600] sometimes still held part of the loading screen. The loading screen counts
  game frames at ~17/s and gameplay at ~1-2.5/s, so those runs read high. That explains spreads like 2.09 vs 1.14
  between runs of the same build.
- `analysis/level_rate.py` now also prints `play` (from the end of the loading screen) and `tail` ([600 s,
  1080 s], gameplay in every run). Tail per pair, with instances swapped:
  - draw batching vs off: 1.03/1.15/1.23/1.19 vs 0.74/0.79/0.89/0.89 (+39%, 4 of 4).
  - snapshots+uploads vs draws only: 0.90/1.29/1.27/1.32 vs 0.89/1.18/1.17/1.19 (+8%, 4 of 4).
  - clears batched vs not: 0.87/0.94/0.84/1.27 vs 0.85/0.93/0.85/1.26 (+0.7%, noise).
  - the measuring profilers (`SPURS_PM_PROF`, `GOW_MAIN_PROF`, `PS3_POLLTOP`) off vs on: 1.42/1.36 vs 1.40/1.34
    (+1.6%). Measurements taken with them on are not skewed by more than that.
- Decrementer clock: the TSC path gains ~45 us a minute on QPC (0.75 ppm), so it does not distort game timing.
  `SPU_HOST_NS=qpc` switches back for A/B. The two QPC runs did not reach the part of the level where the texture
  garbage usually appears, so they say nothing about it either way.
- Where the level is now: ~1-1.4 game frames/s late in the level (the boss fight), ~1.6 right after loading. The GPU
  wait is no longer the limit; the CPU side is (SPU job cost and how many host threads run SPU work).
Update (M0.56, 2026-10-22): **in gameplay, two chains of about the same length.**
Measured from a `PS3_FTRACE` timeline at 700-720 s, where a frame is ~500 ms:
- **Walker chain.** The RSX walker is busy ("drain") ~400 ms per frame and never waits on a semaphore ACQUIRE.
  ~170 ms of that is waiting on the GPU at batch flushes: `vk_eng_submit_and_wait` (315 x 10 ms) and `vk_begin`
  (146 x 11.7 ms). The submit-and-waits come from coherence readbacks (a surface read back into guest memory) and
  wait for the whole open batch.
- **SPU / main chain.** The main thread waits ~353 ms per frame for the WWS job lists (`0x3E8388`) and ~32 ms in
  the game's frame pacing.
  - The pacing is `0x375A60`: `usleep(30)` until the flip counter at `*(0x9BE004)+8` reaches the game frame
    counter (`0x9710C8`) − 1.
  - The kernel threads are not saturated. Audio (wid 8) keeps one thread 81% busy. The other two are ~73% busy,
    with EDGE (wids 15-17, ~1500 jobs per game frame) taking ~85% of a thread's worth.
  - EDGE ch8 is down to 3-5%, so the ring-space backoff is mostly gone. The job lists are bound by the latency
    of the job chain, not by thread count.
- `VK_ASYNC=1`: batches are submitted without waiting, with up to 3 in flight. The GPU sync wait went from 199 to
  81 ms per frame, but frames did not move (swapped 2+2, tail 0.918 vs 0.917): the SPU chain is as long as the
  walker's. It stays off by default.
- `PS3_FTRACE` now records SPU busy spans for SPURS module runs, tagged with kernel thread, image and workload. In a
  heavy scene the SPUs are 232% busy of 300%: audio is 34% of that, EDGE 42% (~800 ms of SPU work per ~835 ms
  frame).

Update (M0.57, 2026-10-22): **native SPU single-precision floats** (`SPU_FP_NATIVE`, port build option
`GOW_SPU_FP_NATIVE`, on).
- The lifted SPU code emulated `fa`/`fs`/`fm`/`fma`/`fms`/`fnms` bit for bit, through doubles, with checks. The SPU
  rounds singles toward zero, so with the host's MXCSR set to round toward zero (every `spu_run_with_halt`) a
  native add, multiply or FMA3 gives the same bits for ordinary operands. Other lanes still take the exact path,
  and double-precision ops put round-to-nearest back around themselves.
- `SPU_FP_CHECK=1` compares every op with the exact path: 1.34e9 ops through boot and gameplay, 0 mismatches.
- Swapped 2+2 A/B, tail: 1.175/1.269/1.098/1.031 vs 1.169/1.242/1.042/0.998 (+2.7%, 3 of 4 pairs). EDGE per-job cost
  went down 5-20% (`[pm-prof]`).
- Frames moved much less than the job cost. Per frame, the EDGE work (wids 15-17, `[pm-prof]`) divided over the two
  non-audio kernel threads is 283-375 ms. The main thread's wait on the job lists is 300-398 ms. Those track
  each other in six runs, so the main thread waits on EDGE throughput over two host threads.
- A fourth kernel thread with audio still on its own (`SPURS_KERN_SPUS=0,1,2,5`, swapped 2+2) is neutral: play
  1.096/1.192/1.122/1.103 vs 1.110/1.144/1.190 (plus an outlier at 0.688), with no hangs. Total kernel-thread busy
  only went from ~200% to ~210%.
- Why: the per-thread SPU timeline (`PS3_FTRACE`, 20 s at 700 s) shows **the three EDGE workloads (EDGE0-2, wids
  15-17) never run at the same time (0.0% of the time)**. One kernel thread runs 12 s of EDGE per 20 s. The other
  non-audio thread is idle 59% of the time, 48% of it while EDGE runs elsewhere. The game splits geometry into three
  workloads (each registered with maxContention 1) so they run side by side on three SPUs; here they run one after
  another.
- `SPURS_EDGE_DIAG` confirms it: at every EDGE dispatch (~155k a minute) no other EDGE wid is busy, and none
  has readyCount up. The EDGE runs come in bursts on one thread (1.9 us between runs). Thread hand-offs happen
  only ~1300 times in 20 s, and the EDGE lists are busy 15.9 s of every 20.
- `SPURS_MAXCONT=15=2,16=2,17=2` (two lanes per EDGE list) is slower: tail 0.785 vs 1.402, because a host thread
  is created per lane per dispatch.

Update (M0.58, 2026-10-23): **three defaults that pay together.** Each A/B is swapped 2+2, scored on tail, and
stacked on the previous one.
- `SPURS_PM_RERUN_US=2000` (a module run that executed jobs is re-entered at once, for up to 2 ms): 1.494/1.390/
  1.402/1.419 vs 1.392/1.271/1.294/1.298 (+8.5%, 4 of 4). EDGE jobs per run 0.88 -> 0.97.
  - The WWS policy module hands the SPU back after each job, so a list had advanced one job per kernel pass.
  - At M0.52 this setting gave 0% in frames, while the walker was 66% GPU-blocked and EDGE waited on ring space.
- `SPURS_KERN_SPUS=0,1,2,5` (a fourth kernel thread, SPU 2: EDGE0-2 have priority 8 on SPUs 0-3): 1.542/1.467/
  1.508/1.479 vs 1.429/1.371/1.402/1.400 (+7%, 4 of 4). Without RERUN it had been neutral.
- `VK_ASYNC=1`: 1.590/1.523/1.525/1.512 vs 1.502/1.435/1.456/1.415 (+5.9%, 4 of 4). Alone it had been neutral; it
  pays now that the SPU chain is shorter than the walker's.
- Together: tail ~1.31 -> ~1.54 game frames/s (+17%). 16 runs with no hang, no `invalid size`, no BRANCH-TO-0.
  The texture garbage below shows at the same rate in both arms.
- The port sets all three (`main.cpp`). Each can still be set back by its variable.
- Next: the walker's per-draw full barrier (`vk_cmd_after_everything`, ~60 us of GPU per draw) is the next GPU-side
  cost, now that the GPU sometimes runs three batches behind. On the SPU side, EDGE still runs one list at a time.

Update (M0.59, 2026-10-23): **timer resolution, upstream fixes, and an intermittent level hang.**
- Windows 11 ignores `timeBeginPeriod(1)` for a process with no visible window. Offscreen, each 1 ms pause of the
  SPURS kernel threads took ~15 ms (PS3_FTRACE: 12-16 ms per pause, 22-75% of a thread's time).
  - The port now opts out of that throttling (`SetProcessInformation(ProcessPowerThrottling)`); `GOW_TIMER_FIX=0`
    skips it.
  - Pauses dropped to ~2 ms and the level is reached at 120-180 s instead of ~240 s.
  - Steady gameplay barely moved: tail 1.569 vs 1.544 in the one clean pair.
- Taken from open upstream PRs: #492 (`event_status` updated atomically), #491 (`SPU_IRQWAIT_DIAG`) and #522 (PPU
  reservation re-checked under the lock-line lock). The triage of all 116 open upstream PRs is a local working
  note, not in this repo.
- An intermittent hang in the level (fixed in M0.59e below): an EDGE job spins in its ring-space backoff
  (`pc=0xC834`, `r3=0x960`) after the RSX walker stops flipping, then `[pm-hang]` repeats.
- Also from upstream: #533 (the SPU drain's per-hop hooks run their common case inline) and #534 (guest stores
  test the SPU reserved-line bitmap inline). A/B against the build without them, swapped 2+2: tail 1.508/1.510/
  1.656/1.540 vs 1.417/1.477/1.579/1.515 (+3.8%, 4 of 4), with no hang or bad size in 8 runs.
- Also from upstream: the draw-engine CPU series #458 (vertex fetch attribute by attribute), #465 (index runs in one
  pass), #432 (linear A8R8G8B8 decode), #438 (texture key location cache), #439 (no full hash for engine-rewritten
  textures), and #500 (SPU estimates, interpolate and float/int conversions in AVX2, rounding-mode independent).
  Swapped A/B, stopped after two pairs that agreed: tail 1.379/1.331 vs 1.308/1.248 (+5.4%, +6.7%), no hang.
- Also from upstream: #512 (SSE4.1/AVX2 forms of the SPU per-lane integer and quadword helpers; our SSE quadword
  forms kept). Three clean pairs: tail 1.400/1.273/1.350 vs 1.356/1.252/1.294 (+3.2%, +1.7%, +4.3%). The fourth
  pair hung in both arms at the same scene.

Update (M0.59e, 2026-10-23 night): **the level hang was the Vulkan driver's memory calls; fixed.**
- The watchdog (`GOW_CLOCK_WATCH=20`) showed the walker on a `SET_BEGIN_END(0)`, with put far ahead and flips still
  coming every ~5 s. It was not waiting on a semaphore, a self-JUMP park or a GPU fence (`vk_fence_waiting=0`), so it
  was crawling through the draw engine's END.
- `ENG_PHASE_STATS=<s>` (toolkit) splits the engine's time per window by phase and by texture path. During a hang,
  single texture binds took 15-24 s, all inside depth/colour readbacks to guest memory and texture re-uploads.
- `VK_SLOW_MS=<ms>` (toolkit) times the driver calls. `vkFreeMemory` took up to 2.35 s, `vkAllocateMemory` 1.5 s
  and `vkQueueSubmit` 1.6 s, none of them fence waits.
  - Each readback allocated, mapped and freed a host buffer.
  - Each changed texture was re-created (an allocation and a synchronous layout submit). The old one was then
    destroyed, which waits for every batch in flight and frees.
- Not the same scene, as M0.59 assumed: the two arms of a pair hung at different times (120 s vs 270 s, 450 s vs
  510 s). They share one GPU and its kernel memory manager, which is how one instance's stall reaches the other.
  VRAM peaked at 3.3 of 8 GB (`nvidia-smi`), so capacity is not the cause; the volume of calls is.
- Fix (toolkit):
  - Readbacks and unbatched uploads reuse one grow-only staging buffer each.
  - A changed 2D texture is rewritten in place when the backend orders uploads after recorded draws
    (`rsx_draw_backend.texture_upload_ordered`, set by Vulkan), with the same mip count; otherwise create-and-swap
    as before.
- Hangs: 0 of 4 pairs with the fix (8 runs), against 4 of the 5 pairs before it.
- Walker time per 10 s of level (one pair each):

  | | before | after |
  |---|---|---|
  | texture binding | 5.3 s | 1.4 s |
  | all ENDs | 7.7 s | 4.1 s |
  | re-upload | 29 ms per call | 2.7 ms |
  | depth to guest | 47 ms per call | 8 ms |

  Frames/s, swapped A/B against runL, the last exe before #512 and this fix (no diagnostics on): tail 1.738/1.777 vs
  1.571/1.631. That is +10.6% and +9.0%; stopped after two agreeing pairs, with no hang in either arm. #512 accounts
  for about 3% of it (M0.59d), so this fix is worth roughly +6-7%. Level entry also came sooner: 120-180 s vs
  150-210 s.
  EDGE's wait for RSX ring space (`[pm-prof-ch] ch8`, wids 15-17, after 600 s) fell from ~13% of its time (x5, x9)
  to 1-7% (x12, x13).
- Still slow: `vkQueueSubmit` (up to 1.8 s, ~80 per run) and image frees from evictions and target re-creation.
  Every texture still has its own `VkDeviceMemory`; sub-allocating from large blocks is the next candidate.
- No texture garbage in the dumps of these runs (8-px chroma detector, all below 0.012).

Update (M0.60, 2026-10-24): **what bounds the frame now: the EDGE chain, one job at a time.**
- `PS3_FTRACE` over 20 s of the level (600-620 s, runP, one instance): frame median 463 ms. The main thread
  sleeps 310 ms per frame at `0x3E8388`, waiting for WWS job lists. The walker drains 344 ms per frame. GPU waits
  are down to 34 ms.
- The busiest SPU (93%) runs wid 8, `Wws_Job/BrBDblBufWrkld`, almost alone. That is the game's Boomrang audio
  engine (`src/common/boomrang/api_brb.cpp`, Scream plugin packs), which the title pins to SPU 5 (`priority per
  SPU: 0 0 0 0 0 1 0 0`). It is real-time audio, not per-frame work, so it is not on the frame's path.
- The EDGE workloads (wids 15-17, `Wws_Job/EDGE0-2`) are:
  - busy for 96% of wall time (union of their spans);
  - overlapping each other for 15 ms in 20 s;
  - active during 98.8% of the main thread's waits.

  So the frame is the EDGE chain, and it runs serially. Each wid runs ~1100 jobs/s, with a median span of
  0.18 ms (`pmprof_edge.py`: 0.29 ms per job). No run is idle (`z0%`).
- Serial by design, most likely. `SPU_ATOM_WIDS=<s>` (toolkit) lists the lock lines each workload's atomics touch.
  Each EDGE wid has its own line (its `AddWorkloadWA` data). All three also share `0x83257500`: a ring allocator
  (a write pointer in `0x8326xxxx`, and one in the `0x81966300 + 0x60000` command ring). They update it with
  GETLLAR + PUTLLUC: a read-modify-write with no conditional store, which is only correct if one EDGE runs at a
  time. Checking `wklCurrentContention` on a real PS3 (HEN) would settle it.
- The lever is the cost per EDGE job.
  - Host samples on the EDGE threads: roughly 20% runtime dispatch (`spu_indirect_branch`, `spu_lookup`,
    `spu_drain_call`, `spu_step`). The rest is lifted code, hottest `jobmod_d935...` `E550`/`73E0`/`A880` and
    `jobmod_a67e...` `4AB0`.
  - The lifted code keeps every GPR in `ctx->gpr[]` in memory. Each LS store, a char-type `memcpy`, forces `r1` and
    the LS pointer to be reloaded. Each LS write also tests two global watch flags and the LBP-only `0x1320`
    probe.
  - Next: compile those probes out of the lifted SPU code, and get a fuller profile. Keeping GPRs in locals (lifter)
    is the larger lever and needs a spike first.

Update (M0.61, 2026-10-24): **LS probes out of the lifted SPU code: +12-13% frames/s.**
- The toolkit's `SPU_LS_PROBES` (default 1) gates the probes and the watch checks in
  `spu_ls_read128`/`spu_ls_write128`. The port builds its lifted SPU sources with `SPU_LS_PROBES=0` (CMake
  `GOW_SPU_LS_PROBES`, default OFF).
- An LS store in the hot EDGE function went from ~17 host instructions to 7. Three things went away: the LBP-only
  `0x1320` probe and two global flags reloaded after every store. The `pshufb` mask now stays in a register.
- Swapped A/B against the build with probes (runR vs runQ), both pairs agreeing, no hang:

  | | with probes | without |
  |---|---|---|
  | tail, pair 1 | 1.796 | 2.017 (+12.3%) |
  | tail, pair 2 | 1.831 | 2.069 (+13.0%) |
  | EDGE ms/job, pair 1 | 0.343 | 0.302 (-12%) |
  | EDGE ms/job, pair 2 | 0.329 | 0.295 (-10%) |

  The level tail is now above 2 game frames/s.
- The host sampler took one round per ~430 ms, a system-wide thread snapshot each time, so profiles held a few
  hundred samples per thread. It now keeps thread handles. `PS3_SAMPLE_RANKS` raises the RVA buckets per report.
- `SPU_FP_CHECK_HOOK=0`: the `SPU_FP_CHECK` call after every native `fa`/`fm`/`fma` is never taken, but it is an
  outside call, so registers held in `ctx` were reloaded after each float op. The port turns it off with the LS
  probes (same CMake option).
  - In the three hottest EDGE functions: calls -45-50%, host instructions -17-23%.
  - A/B runS vs runR, 4 swapped pairs, all positive, no hang: tail +3.9/+4.8/+4.7/+4.8%; EDGE ms/job -2.5% to
    -5.4%.
  - The fourth pair finished on its own after the batch script was stopped for low memory; its timeouts were still
    running.
- Profile with the fixed sampler (~43k samples per 10 s): on the EDGE threads ~45-49% is lifted code and ~22% is
  dispatch (`spu_lookup`, `spu_step`, `spu_indirect_branch`, `spu_drain_call`). Another ~6-7% is the interpreter
  (`spu_step`, `spu_interp_run_until_impl`), so some EDGE code still runs unlifted. Next: `SPU_INTERP_MISS=1` and
  a re-lift of what it finds.
- A static study of keeping GPRs in locals (lifter) found it feasible. No function is entered except at its start,
  and only `_u32[0]`/`_u16[0]` views are used. It needs a shadow-run harness (two lifts compared from one
  snapshot) before it can ship. It is parked behind the cheaper items.
- Re-lift from a `SPU_INTERP_MISS=1 SPU_LSA_GETLOG=...` run of the current build.
  - In that run the EDGE wids interpreted ~1.5 s per 20 s, mostly fragment placements not lifted yet (image 225 at
    LS `0x11730`, `0x112E0`, ...).
  - `lift_edge_frags.py`: 361 -> 438 placements.
  - `interp_miss_to_extra.py`: +40 job-module entries in `spu/jobmod_extra.json`, then `lift_jobmods.py`. 42 more
    targets lay past their module's image (module end = LS `0x4000` + its size), d935's at `0x11xxx` and 759d's
    above `0xEA66`. Those are not module code and were left out.
  - Swapped A/B against the build before the re-lift, no hang, the same 40 "unresolved" lines in both:
    - pair 2, both arms entering at 120 s: tail 2.212 vs 2.050 (+7.9%), EDGE ms/job 0.285 vs 0.302;
    - pair 1: 2.219 vs 1.848 (+20%), ms/job 0.267 vs 0.327. Its arms entered the level at 120 s and 210 s, so this
      pair overstates the gain.
  - A second `SPU_INTERP_MISS` run on the new build: EDGE interpreted time fell from ~650 to ~48 ms per 20 s per wid
    (-93%), and all workloads together from ~4 s to ~1.4 s per 20 s.
  - Known item, not chased: wid 7 (`Wws_Job/BatchJobList`, job module 759d) interprets ~1 s per 20 s. That code
    sits at LS `0x16670`-`0x359C0`, outside the module's image, staged somewhere the lift never saw (the WWS
    manager's second code buffer is a candidate; `SPU_JOBMOD_LOG=1` would show the code base). It is not on the
    frame's path.
- Profile of that build (runU, fixed sampler, 600-720 s):
  - On the three EDGE threads, ~50% is lifted code. Dispatch takes ~15-16%: `spu_indirect_branch` 5.5,
    `spu_drain_call` ~4.8, `spu_lookup` ~2.7, `spu_overlay_note_get` ~1, `spu_step` ~1, `spu_run_with_halt` ~0.9.
    MFC helpers take another ~5%.
  - Inside the hottest lifted functions (E550, 73E0, 4AB0, A880) the samples spread over 600-1400 16-byte buckets,
    with no hotspot. By instruction class:

    | class | share |
    |---|---|
    | vector/ALU | 48-57% |
    | `ctx` memory | 11-18% |
    | stack spills | 11-17% |
    | `pshufb` | 4-7% |
    | constants | ~5% |
    | LS memory | 1-3% |

  - Keeping GPRs in locals would mainly remove the `ctx` traffic and some spills, which is an upper bound of
    ~8-12% of an EDGE thread. Days of work and a new test harness, so it stays parked.
  - The dispatch path is the nearer target, but it is hang-sensitive (interrupt frames, setjmp in
    `spu_drain_call`).
- `scratchpad`-style helpers used for this (not in the repo): a 16-byte bucket classifier over the link map and
  the exe (capstone), and an asm counter per lifted function.
- Texture garbage is still there, but rarer. Over the 30 runs since the hang fix that have dumps at f_06600 and
  later, the 8-px chroma detector flagged 2:
  - `x15qs_1` (runQ): 5 frames, up to 0.058; noise on the golden wing meshes;
  - `im_2` (runU): 1 frame, 0.018; tiled columns in the background.

  Of the 18 runs that reach f_09000, that is 2, against 6 of 22 before. Both counts are small, so "rarer" is not
  established. The cause is still open.
- **Checked on a real PS3 (HEN, webMAN MOD PS3MAPI, read only; the console runs patch 1.12, our lift is 1.00).**
  - The game's `CellSpurs` sits at `0x3001AD00` (ours: `0x8001AD00`, the same offset in its region).
    `wklInfo1` is at `+0xB00`, `wklInfo2` at `+0x1000`, and there are 23 workloads.
  - The wid numbers differ from ours. The EDGE workloads are wids 19-21 there: their `arg` pointers are
    `0x14280` apart, exactly as our wids 15-17 are. Boomrang audio is wid 12 there (SPU 5 only).
  - 700 samples of `wklCurrentContention` (+0x20, high nibble for wids 16+), taken while walking in the first
    level: two or more EDGE wids running at once in **0** samples; exactly one in 349 (50%).
  - So EDGE is serial on the real console too, by the game's design. On hardware it occupies about half the wall
    time at full speed; here it fills 96% of a much longer frame. The lever stays the cost per EDGE job.
  - Also seen: SPURS on the console spreads EDGE over SPUs 0-5 (priority 3 on 0-3, priority 8 on 1-2 for wid 21).
- **Patch 1.12 vs our 1.00.**
  - The patched `GOWA.SELF` would not decrypt with our RPCS3 (`Failed to decrypt SELF metadata info`). Its loaded
    image was read from the running game through PS3MAPI instead. It stays outside the repository.
  - Code is +176 KB (+2.2%). The data segment moved from `0x800000` to `0x820000`, and `.bss` grew ~283 KB.
  - 53% of the 1.00 code's 64-byte blocks appear unchanged, at dozens of different shifts. The binary was relinked,
    so every address the port uses would move.
  - 12 of the 33 embedded SPU job modules changed, among them `d935`, the main EDGE module.
    `d935` itself barely changed (checked 2026-10-25, word by word over the module, same size and entry): 6 of its
    12576 words differ, 4 in the module header and 2 in one function where the two instructions that zero r28 and
    r32 swapped registers (same result). E550 and 73E0, the native stages, are byte-identical at the same
    addresses, so the native code carries over to 1.12 as is.
  - Moving to 1.12 means a full PPU re-lift, those 12 modules and the EDGE fragments re-lifted, and every hardcoded
    address remapped (best done with a function-matching tool). That is several days. Parked as a milestone for
    before any release, since most players will have 1.12 installed.
- **SPU MLAA off: no gain.**
  - RPCS3's public "Disable SPU MLAA - God of War: Ascension" patch (`il r2,0` at LS `0x4440`, `il r10,0` at
    `0x56C4`) matches our `gowtask0` (image 14): the original instructions there write exactly r2 and r10.
  - Both arms were re-lifted with the current lifter from a patched copy of the EBOOT and from the original one.
    A/B, no hang:
    - pair 1: tail 2.283 (MLAA off) vs 2.396 (on);
    - pair 2: 1.971 vs 1.950.

    EDGE ms/job is flat as well. MLAA runs outside the serial EDGE chain, so the frame does not shorten. The patch is
    not kept.
  - Side effect of the re-lift: the seven SPU images (`images.json`) are now lifted with the current lifter (a
    `_bt`/`_tg` rename, and `srepm0`'s `stop` now carries code `0x3FFF`). No hang in the four runs.
- **M0.62: the RSX walker had become a co-bound; a Vulkan backend fix frees it.**
  - A `PS3_FTRACE` of the current build (600-620 s, one instance) put the walker's drains at 470 ms of a ~526 ms
    mean frame, up from 344 ms at runP: the EDGE gains had moved the walker close to the bound. 3.0 s of the
    20 s were synchronous GPU waits from readbacks and texture creates.
  - `ENG_PHASE_STATS`: per 10 s the walker spent 3.7-4.1 s in the backend draw call (~65 us per draw), 1.9-3.6 s
    in textures and ~0.6 s in vertex fetch.
  - `VK_DRAW_PROF` (new) split `vk_eng_draw`: 41 us of it was the uniform copy. The engine wrote the shader
    constants straight into the shared UBO, and each batched draw read them back out into its ring slice. That UBO
    is host-coherent memory, write-combined and uncached on this driver (RTX 3080, NVIDIA 617.42), so reading
    ~8 KB took ~40 us. The constants now live in ordinary memory.
  - After the fix: descriptor work 41 -> 2.5 us per draw, backend 3.7-4.1 -> 1.0-1.3 s per 10 s, walker busy
    7-8.8 -> 3.9-5.4 s per 10 s.
  - Game frames/s barely move, because the frame is again bound by the serial EDGE chain. Swapped A/B, runY vs
    runX, no hang: tail 2.558 vs 2.527 (+1.2%) and 2.267 vs 2.206 (+2.8%). EDGE ms/job flat, EDGE ring waits
    (`ch8`) slightly lower. The walker now has slack for when the EDGE side gets faster.
  - Textures sampled through guest memory (`[eng-via-guest]`, new): the 1280x720 D24S8 zeta read as A8R8G8B8
    (fmt `0xA5`/`0xE5`), RGBA8 targets read as Y16_X16_FLOAT (`0xBF`), and sub-rectangles of the 1408x1408
    shadow map. These are the remaining readbacks if the walker becomes the bound again.
  - Upstream: the readback is in PR #532's batching path; the fix is pushed there.
- **M0.63: the WWS interrupt handler's generated stubs, +3.5%.**
  - A per-workload profile (`PS3_SAMPLE_WIDS=15,16,17`, new) shows where EDGE time goes:
    - lifted code 73%;
    - `spu_indirect_branch` 7.0%;
    - `spu_drain_call` 5.1%;
    - `spu_lookup` 1.6%;
    - MFC ~4%.

    The earlier "15% dispatch on the EDGE threads" was mixed with the audio workload, which makes ~4M of the ~4.4M
    indirect branches a second (`SPU_IB_STATS`, new).
  - EDGE's ~400k indirect branches a second are mostly the WWS policy module's stall-and-notify handler (LS
    `0xA2C`). On every interrupt it generates a stub at LS `0x3FEC0` that saves r0-r75, built from the template at
    `0xA40` minus `0x804` per quadword, plus a `bi $r79`. It calls the stub, runs the handler at `0x2390`, then builds
    and calls a restore stub (template `0xAD0`). That is ~13k interrupts a second per EDGE workload. The stubs only
    exist at runtime, so they ran in the SMC microstepper at ~25 ns an instruction.
  - The microstepper now runs straight sequences of `lqa`/`stqa`/`lqd`/`stqd` in a tight loop, with raw
    byte-swapped copies and no per-access probe. This only applies while every LS diagnostic is off.
    `spu_indirect_branch` went from 7.0% to 4.2% of EDGE samples.
  - Swapped A/B, runZb vs runY, no hang:
    - tail 2.688 vs 2.598 (+3.5%), EDGE ms/job 0.225 vs 0.232;
    - tail 2.712 vs 2.621 (+3.5%), EDGE ms/job 0.221 vs 0.230.
  - The handler itself (`0xA2C`) was no lifted entry of the policy module, so its stub-builder loop (`0xA60`)
    ran in the microstepper and then the interpreter (`stqx` is outside the microstep set). It is now an extra
    entry of `gowpm0` in `spu/images.json`. No EDGE workload is left in `SPU_INTERP_MISS`. A/B runZc vs runZb, no
    hang:
    - tail 2.663 vs 2.610 (+2.0%), EDGE ms/job 0.219 vs 0.223;
    - tail 2.792 vs 2.750 (+1.5%), EDGE ms/job 0.216 vs 0.220.
- **M0.64: `longjmp` without unwinding, +6-7%.**
  - Even with the A/B harness's per-channel timers off, 8.4% of the EDGE samples were in ntdll. `PS3_SAMPLE_MOD`
    (new) put 60% of those in `RtlVirtualUnwind2`, `RtlLookupFunctionEntry`, `ZwContinueEx` and `RtlUnwindEx`.
  - On 64-bit Windows, `longjmp` unwinds every host frame back to its `setjmp`, and the SPU runtime longjmps on every
    halt (each policy-module run's exit) and every iret.
  - `spu_jmp_no_unwind` zeroes the `jmp_buf`'s `Frame` right after `setjmp`, so the jump only restores registers. The
    jumps cross only C frames of the runtime and of lifted code. ntdll dropped to 2.8%, all condition-variable waits.
  - Swapped A/B, runZe vs runZc, no hang:
    - tail 2.935 vs 2.744 (+7.0%), EDGE ms/job 0.205 vs 0.220;
    - tail 2.802 vs 2.654 (+5.6%), EDGE ms/job 0.208 vs 0.222.
  - The A/B harness's `SPURS_PM_PROF` itself costs ~5% of EDGE time: two `QueryPerformanceCounter` calls per channel
    op. It is not set in normal play, so absolute tail numbers here are slightly pessimistic.
  - Level tail today: ~2.2 (runU, morning) -> ~2.8-2.9 game frames/s.
  - Bound re-checked on runZg (`PS3_FTRACE` 600-620 s, one instance): 74 frames, median 283 ms, against 38 frames
    on this morning's build. The main thread waits on EDGE for 168 ms per frame; the walker drains for 163 ms
    (~60%). EDGE ring waits (`ch8`) are ~1%, so the frame is still EDGE-bound.
  - EDGE-only profile now: lifted code 79%, runtime ~20% in pieces of 1-4% each (`spu_drain_call` 4.0%,
    `spu_indirect_branch` 3.7%, MFC ~6%, `spu_lookup` 1.5%), ntdll 0.2%.
  - 31-minute soak of runZg without harness instrumentation: no hang and steady memory. The later level runs at
    ~3.6-4.0 game frames/s (game frame 7951 -> 8611 between 1650 and 1830 s).
  - Inside the five hottest EDGE functions, context-memory accesses are 11-24% of samples and stack spills 11-16%.
    Register promotion in the lifter is the remaining double-digit lever, with a risk of trading context loads for
    spills.
- **M1.04: the reservation-holder walk is an array scan (2026-11-02).**
  - Why: after M1.03 the lock line is mostly PUTLLC and PPU stores to marked lines, and each of those walks
    every registered holder with the line held (38 in GoW:A). x301 put 77% of `spu_coh_notify_write` on the
    load of a holder's `resv_valid`; the linked list added a miss per holder for its malloc'd node.
  - Change (toolkit `spu_coherency.c`): `s_coh_ctxs` is a contiguous array of context pointers (append on first
    reservation, swap-remove on unregister and `spu_coh_forget_range`), and the notify walks prefetch
    `resv_valid` four holders ahead. Same lock, same membership rules, same Dekker argument.
  - Result (x308 = x301 recipe on runXc, loaded EDGE profile from 120 s, against x307): `spu_coh_notify_write`
    2.92% -> 1.16% of EDGE samples, `spu_wrch_impl` 1.50% -> 1.08%; `spu_lockline_lock` 2.09% -> 2.20%. 480 s,
    no stall. Both runs had M1.03 in. The shipped state (M1.04 with the ack lock back), x313 on runXd with the same
    recipe, against x301 (M1.02): `spu_lockline_lock` 3.33% -> 2.26%, `spu_coh_notify_write` 3.19% -> 1.16%,
    `spu_wrch_impl` 1.88% -> 0.78% of EDGE samples; 13 waits over 5 ms in 480 s (M1.02's exe: 1-38 per run on
    this recipe). Tails with five busy processes: x310's locked arms, none over 5 ms. Upstream: PR #672.
  - Quiet baseline, whole process (x312: runXd, 480 s, pad masher only, PM/main profiling on, `PS3_SAMPLE` with no
    workload filter, samples from 120 s; 13.0-13.5 game frames/s on this recipe):
    - The two busiest host threads are a PPU thread (30% of all samples) and the RSX thread (28%). Every SPU
      thread is far below them (13.5%, then 6% each).
    - The PPU thread is 44% in game function `00416338` (the job-list counter poll noted in the a67e work) and
      31% in `00466E38`: it mostly waits.
    - The RSX thread: `rsx_vertex_fetch_many` 26%, `gcm_rsx_process_fifo_unlocked` 14%, texture content hashing
      17% (`eng_texture_content_hash` 10.7 + `eng_texture_sparse_hash` 6.0), `rsx_vertex_remap_build` 10%,
      `sink_end_draw` 8%.
    - So with the host quiet, the frame's critical path looks like the RSX thread, with the PPU waiting, not SPU
      throughput. What `00466E38` waits on, and whether the RSX thread is the one it waits for, is the next
      question.
    - Critical path, quiet (x315: runXd, `PS3_FTRACE` 20 s from t=300, `tools/ftrace_report.py`): 269 frames,
      median 67.1 ms (14.9 fps), min 50.1, max 173.3. Per frame on average:
      - SPUs: five of the six ~90% busy (61.7 / 59.2 / 60.7 / 61.6 / 60.8 ms), the sixth 48%.
      - RSX walker: in drain 61.6 ms (~92%).
      - The main PPU thread: blocked only 14 ms; the rest is busy, mostly polls. `00466E38` is one too (163 lines:
        reads the u16 completion counters of the job lists under `lwsync` and loops), like `00416338`, so the
        main thread waits; it is not a third saturated resource.
      - The walker's drain is its own work, not a wait for SPU-built commands: the hot buckets of
        `gcm_rsx_process_fifo_unlocked` (x312) are the per-method decode and dispatch (a byte swap, method
        compares, one atomic exchange per method), not a wait-for-put loop.
      - The window is 20 s of one corridor section (max frame 173 ms: a loading spike is in it); a baseline, not
        the whole game.
      - The walker's biggest item, vertex fetch, was worked before (see "Vertex fetch conclusion" further down):
        SIMD and GPU decode were both exact and neither paid, because the cost is the walker's first touch of the
        vertex bytes the SPU threads just wrote, not the decode. With the walker at ~92%, what is left is moving
        work off its one thread (fetching ahead on another core, or splitting FIFO decode from draw building),
        and the GPU path's gather for deduplicated references (only ~55% of draws qualified). Multi-day items.
      - Two resources are near saturation together, the SPU pool and the single RSX walker thread. That is why
        cutting one stage (General6 -47% at M0.88, texture hashing in x314) barely moves the frame: the other
        one takes over as the limit. A real step needs both to gain, so the RSX walker (vertex fetch 26%, FIFO
        14%, texture hashing 17%, remap 10% of its samples) is now as much a target as SPU throughput,
        including taking work off its one thread.
    - Texture hashing tried first (x314, quiet, same exe runXe, `PS3RECOMP_ENG_TEXHASH_EVERY` unset = 8 against 64,
      warm-up then A B B A, 480 s): 13.23 / 13.16 / 13.66 / 12.63 game frames/s over 360-480 s, 11.21 / 11.74 /
      12.17 / 11.57 over 120-480 s. B is ahead by ~4-5% on average, but the first pair is level: a few percent at
      most, inside the noise. Not the lever, so the default stays 8 (rarer full hashes would leave a small write
      to a big texture unseen for longer). x246's `[eng-stats]` had put ~30 full hashes of big textures per frame,
      ~16 MB hashed a frame.
    - Causal probes (x316, 2026-11-03 04:22-05:22, quiet, same exe runXf = runXe + the new diagnostic
      `SPU_SLOW_PCT`, warm-up then O R S S R O, 480 s, pad masher only). R = `RSX_SLOW_PCT=25` (the walker
      busy-waits 25% of each pass), S = `SPU_SLOW_PCT=25` (each SPURS kernel thread busy-waits 25% of each module
      run). A fully binding stage gives -20%. Game frames/s over 360-480 s:
      - O 11.83 / 12.72, R 11.62 / 11.18, S 10.92 / 10.67. The order S < R < O holds in every window
        (300-480, 360-420, 420-480).
      - Raw: R -7%, inside the O arms' own 7.5% spread; S -12%, outside it.
      - The probes are not equal loads: S spins five busy threads (~1.1 extra cores; the game drew 7.6-8.5 cores
        against 7.0-7.3 in O), R one (~0.23 core).
    - Control (x317, 05:30-05:57, same exe, B = plain run plus `burn.py 1`, B O B): B2 12.23 against O3 13.08,
      -6.5% for one normal-priority busy core. B1 is void with a reason: a different trajectory from minute 2
      (36 game frames/s in that minute against ~62 in every other run, then 3 for three minutes) while the game
      drew less CPU (5.0-6.6 cores against 7+), which a normal-priority burner cannot cause in an AboveNormal
      process. It is a different game state, not contention; if a loaded arm does that again it is a robustness
      bug, not noise.
    - Corrected for the extra CPU each probe drew, R and S both land near -5.5%, ~0.28 of a fully binding stage
      each. The co-saturation reading of x315 stands: the walker alone (the lookahead/prefetch design) and the
      SPU side alone are each worth about a quarter of their own gain.
    - The SPU count is not a lever: the port already runs six kernel threads (`SPURS_KERN_SPUS=0,1,2,3,4,5`, the
      game's `nSpus`), and one fewer did not help (x279).
    - The larger finding: one core of ordinary background load costs 6.5% on a quiet host. The game itself runs
      ~7.3 busy logical threads on six physical cores, so its own polling threads sit on the hyperthread siblings
      of its working threads. The main PPU thread is ~0.8 core and blocked only 14 of 67 ms (x315); the rest is
      its two job-list polls (`00416338`, `00466E38`). Making those polls lighter on the sibling is the one lever
      that can lift the SPU side and the walker together, and it is also the robustness direction. x201 (sleep
      every 64th call) judged that by the thread's own CPU, which a pause or yield does not lower; what it
      lowers is what the sibling loses.
    - Three O arms of the same exe and env on a quiet host spanned 11.83-13.08 game frames/s (10%). Levers of
      3-6% cannot be read on frame rate alone here; see the A/B protocol in DEBUGGING.md.
    - Spin hint on the main thread's polls (x318, 2026-11-03 07:21-08:38, runXg = runXf + a `PPU_SPIN_LABELS`
      lift of `0x466EB8` and `0x416338` (toolkit 336ce82e; the exe has the pause instructions in both, the loop
      unrolled), A = `PPU_SPIN_PAUSE=0`, B = 16, warm-up then A B B A quiet, then A B B A with `burn.py 2`, 480 s,
      `PS3_FTRACE` 300-320 s in every arm):
      - Quiet, game frames/s over 360-480 s: A 13.32 / 13.07, B 12.44 / 13.38. Frames in the 20 s trace window:
        A 259 / 251, B 272 / 277. Median frame A 67.5 / 74.1 ms, B 69.2 / 69.7. Walker drain over the window
        16.7 / 16.0 s in A, 15.6 / 16.5 s in B; SPU busy per frame 56.6 / 60.1 against 57.3 / 56.7 ms. The
        two measures disagree on direction, and neither is outside the spread.
      - Loaded (python ~2.0 cores in every arm): A 11.64 / 10.42, B 9.00 / 13.62 game frames/s. lB2 ran faster
        than either quiet A arm with the same burners: the loaded column's spread (9.0-13.6) carries no claim.
      - Neutral at the resolution available. With ~7.3 busy threads on six cores only about one pair shares a
        physical core at a time, so the sibling the polls slow is often idle; the effect a pause can have is
        small. `PPU_SPIN_PAUSE` stays 0 (the hint is in the lift, a no-op unless set). The A arms ran the same
        code: the pause loop is bounded by a global the compiler cannot fold, so with 0 the hint still costs a
        load and a branch per poll round, which makes A a fair control.
    - A still scene as the A/B bench (x319, 2026-11-03): no pad input after the menu, so the player stands at the
      bench checkpoint (frame dumps show the same view in every run). s2 / s3 7.19 / 6.99 game frames/s over
      240-420 s (3% apart, against ~10% for the corridor recipe). The view is heavier than the corridor, so its
      stage shares are not the corridor's. `tools/ftrace_report.py --csv` (toolkit 1664d1c6) writes one row per
      frame for share-of-frame comparisons.
    - A slow mode, now attributed to the host: x319 s1, x320 t1 / t2 and x322 t (the x320 trigger: `burn.py 10`
      for the first 150 s of a still-scene run) ran the same view at 2.9-4.2 game frames/s with less game CPU.
      The main thread waited at `0x3436BC` (`func_003A5AA4`: usleep 30 us per round until the counter at
      `0x983168` reaches the batch size at `0x98317C`) for half the window or more. Wid 0 (`Wws_Job`, image 16)
      jobs bump that counter with PUTLLC, five a batch; the main thread kicks wid 0 (ReadyCountStore, value 8)
      and the kernel picks a kick up in ~0.4 ms in both modes (`[rc-lat]` and the trace marks), but a batch
      took 34 ms (steps ~5 ms) against 1.5 ms. The host was degraded that day (shell commands of a second took
      over two minutes before a reboot). After the reboot (x323, 2026-11-04, same trigger and view, three
      settings) none of the runs went slow: defaults 9.56, `SPURS_IDLE_MAX=0` 9.36, `PS3_USLEEP_WAIT_US=0` 8.96
      game frames/s, and the plain view itself faster than x319's normal runs (~9.5 against ~7.1). Not a game
      bug found; if it shows up again on a fresh host, the scripts are scratchpad `deadgap.py`, `kick0.py`. The
      same symptom (runs that dip slow) has been reported fixed elsewhere by splitting the global lock-line lock
      into per-line locks; post-reboot non-reproduction fits a host cause and a timing-window race equally, so if
      it recurs on a fresh host, lock granularity is the lead.
    - SPU runtime overhead on the EDGE threads (2026-11-04). Still scene, host sampler (x325, runXg, 300 s):
      - Unfiltered, each busy SPU kernel thread spends 43-61% of its samples in dispatch (`spu_indirect_branch`,
        `_slow`, `spu_lookup`, `spu_drain_call`), 14-19% in channels/MFC, 3-15% in coherency and 5-21% in lifted
        code. Most of that is outside the EDGE jobs (policy modules, audio).
      - EDGE only (`PS3_SAMPLE_WIDS=15,16,17`, four threads, 38k samples): dispatch ~26%, native stages ~24%,
        channels/MFC ~15%, lifted ~14%, coherency ~12%, overlay tracking ~5%. By symbol: `g73_loop1` 10.6%,
        `spu_indirect_branch_slow` 10.3%, `spu_lockline_lock` 6.5%, `spu_indirect_branch` 5.7%,
        `spu_overlay_note_get` 5.0%, `spu_wrch` 4.9%, `spu_drain_call` 4.4%.
      - How EDGE's indirect branches resolve (x326/x327, `SPU_IBC_COUNT=1 SPU_IBC_COUNT_WIDS=15,16,17`, new
        `[ibc-miss]` list of the most-missed targets): ~336M dispatches in 300 s, 73% cache hits, 24% misses. The
        single largest miss is LS `0x3FEC0` (~33M, never cacheable): the save/restore stubs the WWS job manager
        generates on each stall-and-notify interrupt, run by the microstepper only after every lookup has missed.
        Then the kernel-service addresses `0x9C0`/`0x9D0` (exit, select workload) and two policy-module targets,
        ~4M each, also never cached; 22.5M misses are conflicts in the 512-entry table.
      - A larger table: `PS3_SPU_IBC_SIZE=4096` (new, default 512) cut EDGE's conflict fills 22.5M -> 4.5M. In
        frame rate it is undecided: x328 (A B B A) read 9.91 / 9.41 / 9.31 / 10.38 game frames/s, but x329 and
        x331 (below) show the middle arms of such a sequence can lose ~10% whatever they run. 512 stays.
      - `spu_overlay_note_get` compared the first 16 bytes of every GET of 512 bytes or more against every
        registered overlay signature in turn; the signatures are now chained by a hash of their first 8 bytes
        (same first-registered-wins result; `PS3_OVL_SIG_SCAN=1` restores the scan; toolkit 489f6382). EDGE-only
        sampler: 5.0% -> 3.3% of the EDGE threads' samples in that function (x325e vs x332e/x333e). Kept.
      - Tried, not kept: a shortcut sending the WWS stub calls straight to the microstepper, and decoded "plans"
        of the stubs' load/store runs keyed by their exact bytes (diff in `work/spu_runtime_x324_x334_full.patch`).
        The shortcut only moved the time (`spu_indirect_branch_slow` 10.3% -> 1.1%, the now separate
        `spu_smc_microstep` 0 -> 10.4%: the lookups were never the cost), and the plans did not lower it
        (10.8%; 3000 cycles a call against 2525 without). The first plan build also hung the game at boot (a failed
        build left a slot's old bytes with half-overwritten operations); fixed before the measurement.
      - What the stubs cost is how often they run: `PS3_SMC_STATS=1` (new, toolkit 489f6382) counts ~290-325k
        microstepper calls a second on the still scene, ~2500 cycles each (~0.2 cores), ~215k of them at
        `0x3FEC0`: about 107k stall-and-notify interrupts a second, not the ~40k noted at M0.63. The lever is the
        interrupt count: whether the runtime raises stall-and-notify more often than the hardware would (once per
        list element with the stall flag) is the next check.
      - Fps on the still scene has the corridor's spread: x329 (A B B A, both changes against neither) read
        10.22 / 8.86 / 8.53 / 10.29, x330 each change alone 9.21 / 9.45, and x331 (B A A B, the same settings)
        9.73 / 9.34 / 9.30 / ~9.1. The B arms lost in the middle of one sequence and led at its ends in the other.
        x319's 3% agreement was luck. For runtime changes the EDGE-only sampler's per-symbol share is the
        measure (it reproduced `spu_smc_microstep` at 10.35 / 10.76 across two runs).
    - Texture dirty tracking by host write watch (2026-11-05, toolkit 78f78789, port default on). The walker
      hashed every cached texture each frame (sparse hash, full hash when it differed or for textures of 16 KB or
      less): 13.3% of its samples. Guest memory is now reserved with `MEM_WRITE_WATCH`; once a frame the engine
      resets the watch and stamps the written pages, and a texture whose pages nothing wrote since its last check
      skips both hashes (a full hash every 64 frames stays as the net). x336/x337 (still scene, one run each,
      same reservation in both arms): ~92k skips per 30 s, full hashes ~41k -> ~2.6k and 4-11 GB -> 0.5-1.5 GB
      hashed per 30 s; walker samples in hashing 13.3% -> 1.1%, 9% fewer walker samples in all, ~5% less wall
      time in draw ENDs (`ENG_PHASE_STATS`, which includes the kernel's write-watch calls). The frame dumps
      match. Frame rate not claimed (9.97 against 9.53, one run each, inside the bench's spread).
      - Check mode (`RSX_TEX_WRITEWATCH=2`: a clean verdict is still sparse-hashed and compared) in the corridor
        with the pad masher (x339, 480 s of loads, streaming and fights): 1.18M clean verdicts, 0 wrong. The
        safety full hash now runs every 16 frames (`RSX_TEX_WW_FULL_EVERY`, was 64), and one reset call covers a
        whole 64 MB chunk (16384 page slots), so a heavy-write frame cannot overflow it.
      - Open: the per-frame reset is a process-wide TLB flush per chunk. In x337 the SPU threads' sample totals
        were 3-5% lower with it on, in line with that arm's ~4% fewer frames in the window; one run each cannot
        separate a cost from noise.
    - The lock line on the still scene (x340, runXn, `SPU_LOCKLINE_STATS=1`, 240 s): ~420k acquisitions a second,
      6% of them contended, 25-30 ms waited per ~2.6 s window (~1% of one thread). So its 6.5% of the EDGE threads'
      samples is the uncontended exchange moving the one lock word between cores, not waiting. 65% of the
      acquisitions come from three sites in `spu_wrch_impl` (the SPU's channel writes), the next from
      `cellSpursReadyCountStore`, `spu_taskset_task_exited` and `mfc_do_transfer`. Per-line (striped) locks
      would cut the bouncing; the call-site inventory is the next step before any code.
    - Striped lock-line design (2026-11-05, not built yet). Inventory of the 29 `spu_lockline_lock` sites outside
      the tests: 26 write one 128-byte line whose EA is known at the call (SPU GETLLAR slow path, PUTLLC, PUTLLUC
      in `spu_mfc_atomic`; PPU `stwcx`/`stdcx` and `VM_WRITE_COH`; cellSync, cellSpurs event flags and ready
      counts, taskset bitsets), 7 cover a range (`mfc_do_transfer` SDCRZ and PUT over reserved lines,
      `vm_memcpy_to`, `vm_memset`, `gcm_row_copy`, `spu_taskset_task_exited`), 3 touch no guest memory
      (`SPU_WrEventAck`, `spu_coh_unregister`, `spu_coh_forget_range`). Plan:
      - 64 stripe locks, stripe chosen by `ea >> 10` (8 lines per stripe), so the `s_coh_bitmap` byte that
        covers those 8 lines stays under one lock (its read-modify-write is not atomic today).
      - The holder list per stripe: `s_coh_ctxs[64]`, `coh_listed` becomes a 64-bit mask; the unlocked GETLLAR
        fast path also requires stripe(ea)'s bit; notify walks only stripe(line).
      - Range and global sites take their stripes in ascending order, or all 64 (unregister, forget_range, the
        ack lock). Lock order stays `resv_lock(L)` then the stripe; still not recursive.
      - Known gap to keep or fix: `VM_WRITE_COH` notifies only the line of `addr`, so an unaligned write that
        spills into the next line is not notified today; with stripes it would also need the second stripe.
      - Gate: the coherency unit tests, then the M1.03 tail test (same exe, env switch, `burn.py 5`, L N N L,
        `[lockline-long]` count and maximum) before any default, judged by the EDGE threads' sampler share.
      - Measured before building (x341, `SPU_LOCKLINE_STATS=1` now also prints `[getllar]`): the GETLLAR fast path
        hits 99.8% (~127k GETLLARs a second), so the hot `spu_wrch_impl` sites are PUTLLC and the event ack. The
        traffic is concentrated: six lines take ~50% of the GETLLARs, `0x81600680` ~9.7%, three lines
        `0x83D57980` / `0x83D6BC00` / `0x83D7FE80` ~8.5% each (one per EDGE workload, by the look of it),
        `0x8001AD00` (the SPURS instance) ~7%. Per-workload lines on separate stripes would stop bouncing one
        word between every lane, so the design still holds; it goes to a fresh context with the inventory in
        DEBUGGING.md. The `[lockline-ra]` sites at small RVAs are the PPU's own stores: `vm_write32` (~28% of
        all acquisitions), `vm_write8`, `ppu_stdcx64`, `ppu_stwcx32`. A line once reserved stays marked in
        `s_coh_bitmap` for good, so every later PPU store to it takes the lock and walks the holder list.
    - A seventh kernel lane for tasksets (an idea from elsewhere: tasksets on an extra SPU, off the EDGE SPUs)
      does not carry over. Every workload GoW:A adds through `AddWorkload` here is a WWS job list (wids 0-18);
      the four `CreateTaskset` workloads never show SPU busy time in the traces (MLAA runs as HLE), and the work
      moved there (MeshProc, GeneralPurpose) is WWS job lists in this port (wid 13 `MeshProcJobs`, ...), whose
      job-list headers only have room for six SPUs. A lane playing a seventh SPU would have to run those, so it
      was written (`SPURS_KERN_LANE_WIDS`) and removed before any run.
    - Striped lock line built and shipped (2026-11-05, toolkit f24e9049, port default `PS3_LOCKLINE_STRIPES=1`; the
      design above, with the per-stripe holder lists). First A/B (x343): the lock's share of the EDGE threads
      went UP (~6% -> ~20%): `SPU_WrEventAck` still took all 64 stripes ~40k times a second. With the ack on
      the stripe of `resv_ea` (the one a notify that raises its event takes), x344 (still scene, EDGE-only
      sampler, one exe, env switch): lock + coherency 13.1% -> 7.6% of the EDGE threads' samples
      (`ll_contended` 6.3 -> 2.0, `spu_coh_notify_write` 2.4 -> 0.9), the native stages' share up. Tail test
      (x345, `burn.py 5`, L N N L, 300 s): waits over 5 ms 8 / 1 / 3 / 5, longest 15.4 / 9.5 / 8.9 / 7.8 ms; game
      frames/s under that load 8.24 / 8.05 / 7.96 / 8.03 (flat). Corridor with the pad masher on the shipped
      defaults (x346, 480 s): no hang, no guard or branch-to-0 lines, 12-13 game frames/s at the end, 4 waits over
      5 ms. `test_spu_coherency` 195/195 with the switch off and on (built with clang on this host). Still on the
      all-stripes path: `spu_taskset_task_exited`, `spu_taskset2_set_exit`, `spu_signalled_update`, the event-flag
      and `AddWorkload` sites, unregister/forget_range. Known gap kept: a store that spills from an unmarked line
      into a marked next line is not notified (as before).
    - The night's defaults together (x351, 2026-11-05, corridor with the pad masher, warm-up then A B B A, 480 s,
      `PS3_FTRACE` 300-320 s; A = runXk before them, B = runXr with texture write watch, striped lock line, O11430
      on all placements and O719900 v3): game frames/s over 300-450 s A 12.01 / 12.75, B 13.34 / 13.58 (360-450 s:
      11.54 / 12.94 against 13.41 / 13.38); frames in the trace window 246 / 257 against 263 / 285; median frame
      75.9 / 75.6 against 73.7 / 65.5 ms; the main thread blocked 23% / 11% of each frame against 6.7% / 6.0%. B leads
      in each pair and in both windows: about +9%, near the edge of this recipe's spread but consistent across
      every measure. Two exes, so code layout differs too.
    - Same comparison after the day's EDGE work (x358, 2026-11-05 evening; A = runXk, B = runXu with every default
      of 11-05: write watch, striped lock line, native EDGE blobs 7228c0 / 719900 / 71fa90 / 71c310 / 723300 /
      7298f0 / 728a90): game frames/s over 300-450 s A 12.43 / 11.73, B 13.33 / 12.30 (+5-7%); main thread blocked
      11% / 11% against 2.8% / 12.6% of the trace windows. B leads in both pairs again, but the host was not quiet:
      another Claude session drew 0.8-1.3 cores through every arm. The EDGE natives' own share (cap_4: about -25%
      of EDGE job time across the seven blobs) is not separable from x351 at this recipe's noise.
    - Double stall-notify interrupt fixed (2026-11-06, toolkit commit after c340bb19's line; scope in
      `work/wws_irq_scope.md`): `SPU_WrEventAck` re-raised the stall event whenever ch25 still held an unread bit,
      and the WWS handler acks before reading ch25, so every real interrupt was followed by an empty one that ran
      both generated stubs. The event is now re-raised only for a stall that arrived after the last event-status
      read (`SPU_SN_REARM_OLD=1` restores the old rule, `SPU_SN_CHECK=1` counts). Corridor, one exe, 480 s each
      (x361): old 50.0% empty takes (24.7M), new 0; microstepper calls 330-433k/s -> 205-247k/s and the stub at
      `0x3FEC0` gone from the top entries; 14.48 / 15.35 game frames/s (one run each, no claim). The busiest
      interpreted entry left is `0x11E64`.
    - 87 missing job-module entries seeded (2026-11-06, `spu/jobmod_extra.json`, 22 modules re-lifted). The SPU
      "enable interrupts" sequence `brsl $2,.+4; ai $2,$2,8; bihz/bihnz $rX,$2` branches to the link + 8, which
      the lifter never made an entry, so every taken branch fell into the microstepper: 0x11E64 (RenderJobs,
      image 221), 0x06774 (EDGE module), 0x086A4 (image 212), 0x0474C. Found by scanning every lift (they never
      reached `SPU_INTERP_MISS`, the microstepper handled them). Still scene 300 s: microstepper calls 202k ->
      169k/s, only the WWS stub at 0x3FEC0 left in `[smc-stats]`. Corridor (x362, 480 s): no assert/branch-to-0/
      drain-mismatch/pm-hang lines, 14.6-15.5 game frames/s in the last three minutes. Not seeded: 13 such targets
      in `gowtask0` (not hot). A lasting fix would have `lift_jobmods.py` seed the pattern itself.
    - The remaining all-stripes lock sites converted (2026-11-06): the taskset exit/signalled updates, the event-flag
      sites and `cellSpursAddWorkload` now lock only what they write; only `spu_coh_unregister`,
      `spu_coh_forget_range` and the unstriped ack branch still take every stripe. They were under 1% of the
      acquisitions, so this removes points where every lock user queued behind one site rather than adding speed.
      Tail test under `burn.py 5` (x363, two runs): waits over 5 ms 3 / 1, longest 8.2 ms, as with the first stripes.
    - The SPU lifter now seeds the `brsl rT,.+4 ; ai rX,rT,imm ; bi* rX` targets itself (2026-11-06, toolkit
      `find_spu_functions.py`, on by default, `SPU_SEED_LINK_OFFSET=0` turns it off). On the 33 job modules it finds
      all 87 hand-seeded entries; with them removed from `spu/jobmod_extra.json` the lifted output is byte-identical,
      so the JSON entries are now redundant (kept). The EDGE fragment, SPU image and BatchJobList lifts will gain
      entries on their next re-lift (untested; the 13 `gowtask0` targets among them).
    - Profile of production runY1 (x364, still scene 300 s, host sampler on the SPU kernel threads): native EDGE
      code ~30% of samples (`g73_loop1` 12.4%), indirect-branch dispatch ~22% (`spu_indirect_branch_slow` 6.7%, the
      fast path 5.8%, `spu_drain_call` 4.8%, lookups 3.1%), channels/MFC ~17%, coherency ~5%, overlay ~4%.
      `[smc-stats]`: ~200k microstepper calls/s, all at the WWS stub 0x3FEC0, ~4200 cycles each.
      The dispatch cache (`SPU_IBC_COUNT=1`, x365): 91.4% hits, 7.5% misses, 23M of the 40M misses filled again
      later. Ruled out as the cause: table conflicts (a two-way table, x366, changed no count). It is capacity:
      `PS3_SPU_IBC_SIZE=4096` (x367) cut fills 23.6M -> 4.5M and misses to 4.1%, but the sampler share of
      dispatch stayed 22.2% (x368: lookups 3.2 -> 1.9%, slow path 7.0 -> 6.5%, fast path 5.7 -> 7.6% as the
      64 KB table leaves L1). Default stays 512, one way. The remaining misses are paths the cache never takes
      (the 0x3FEC0 stub, 0x9C0/0x9D0); the cost is the number of dispatches (~3.6M/s), not the hit rate.
    - The microstepper is inlined into `spu_indirect_branch_slow`; ~75% of that symbol's samples sit in its
      `stqa`/`lqa` loop (the 0x3FEC0 save/restore stub), ~5% of SPU-thread samples.
    - All threads (x371, runY1, quiet, 300 s): the main thread ~59% in its two job-list polls (`0x466E38` 30%,
      `0x416338` 29%); the walker `rsx_vertex_fetch_many` 27%, FIFO decode 14%, `rsx_vertex_remap_build` 10%,
      `sink_end_draw` 9%; one SPU kernel thread 65% dispatch (`spu_indirect_branch` 35%, `spu_drain_call_loop`
      13%). `g73_loop1` (12% of the EDGE threads) is already AVX2 packed.
    - wid 7 (BatchJobList) interprets ~2.4-3.3 s per 20 s again (x369, x373; ~1 s at M0.86), the same with the
      relocatable lifts on (x370). The targets are real code (`SPU_INTERP_MISS` now prints the first words and the
      link register: `ila` + `hbrr`, i.e. hinted loops): `0x359C0` (~1.3-1.8 s, called from 0x35C28) and one
      routine copied to several places (0x18BD0, 0x20BD0, 0x1BF80, 0x23F10 with identical words), which lie in
      EBOOT blocks staged at LS 0x18000/0x20000 (EA 0x7A8C00/0x7B0C00). Not lifted: at M0.86 lifting this
      workload's blobs cut its interpreted time 13.6 -> 4.4 s per 20 s with no frame-rate change, as it is off
      the frame's path. `SPU_LSA_GETLOG` keeps 2048 distinct GETs; it filled even on LS 0x30000-0x36000 (x372).
    - Dispatch cache size closed for every workload: the x368 null result covered the EDGE wids only, but
      General6 alone (x374, wid 3, 512 vs 4096 entries) is 87% its own work with lookups under 1% in both arms.
      The thread with 65% dispatch in x371 runs the audio modules (d3ee/c5a3/5bc2/3105): fast-path hits,
      driven by the call count, not frame-paced.
    - Critical path of production (x375, runY1, quiet, `PS3_FTRACE` 20 s from t=300, corridor): 326 frames,
      median 58.1 ms (17.2 fps; x315 on runXd: 67.1 ms, 14.9 fps), SPUs 1-5 83-87% busy, SPU 0 53%, walker
      drain 52.8 ms (91%), GPU waits 4.4 ms per frame, the main thread blocked 5 ms (the rest polls).
    - Causal probes again (x376, runY1, quiet, O R S S R O, 480 s, game frames/s 360-480 s): O 13.81 / 13.98,
      R (`RSX_SLOW_PCT=25`) 14.96 / 15.18, S (`SPU_SLOW_PCT=25`) 11.73 / 11.85. The walker no longer paces the
      frame (slowing it even helped, likely less contention with the SPU threads); the SPU side does (-15%
      raw against -20% for a fully binding stage, before subtracting the probe's own ~1.1 cores).
    - Which SPU chain (x377, runY4 = runY1 + new `SPU_SLOW_WIDS`, 50% probes, 360-480 s): O 15.04 (warm-up) /
      14.65, E (EDGE wids 15-17) 11.10 (-25%), G (General6, wid 3) 13.57 / 12.18 (-8% / -17%). A fully binding
      stage slowed 50% gives -33%; E also spins ~1.1 cores more than G. The EDGE chain paces the frame most;
      General6 less and noisily. The E2 and O2 arms were not run (the series was stopped for low host memory,
      another session active), so E stands on one arm. Next: the EDGE fragment queue (`work/edgefrag_scope.md`).
    - In-run A/B (new, `GOW_AB=<lever>,<window s>,<start s>`, `scripts/abreport.py`): a lever that can change while the
      game runs goes on, off, off, on... in fixed windows, so drift and host load fall on both sides. x378 (runZ1, 5 s
      windows, 300-680 s, 19 blocks a run): a null lever gives -0.3 ms a frame on a quiet host (16.7 fps) and -0.8 ms
      in a second run (14.7 fps). Under ~2.5 cores of other load (12.4 fps) window noise puts the resolution at about
      +-1.8 ms, which is coarse but costs one run, and that run says nothing about other runs' levels.
    - v0.19.0 picks, no rebase: upstream's per-thread caches for indirect-call targets, profiler breadcrumbs and HLE
      NID lookups, and `ppu_gcm_pump` looking before it locks. Our own: a fast path for the plain indirect call
      (`g_ppu_icall_fast`, about twenty switch tests skipped, `PPU_ICALL_FAST=0` for the old path), and one gate word over
      the MFC path's thirty diagnostic blocks (`g_spu_dma_diag`, `SPU_DMA_LEAN=0`). x378: icall +0.6 ms a frame, dma
      -0.2 ms, both inside the noise, no hang in four 690 s runs. Kept because they remove work and change nothing
      else; neither is a frame-rate claim. Not picked: upstream's `vmx_fmaf` (our PPU code builds for x86-64-v3, `fmaf` is
      one instruction), the parked-task compare-and-swap (ours has it), the address-only overlay reject (our registry
      is hashed).
    - True shares (x379, runZ1, 420 s, `PS3_SAMPLE_RANKS=4000`, buckets cover ~42% of an EDGE thread's non-wait samples
      and 69% of the main thread's). EDGE threads: lifted 30%, native 26%, dispatch 15%, channels/MFC 11%, other 10%, SPU
      other 5%, coherency 2%, overlay 1%. x364's 48% of runtime overhead came from the top 400 buckets, which favour the
      runtime's tight code. About a third of the covered samples is runtime; the uncovered ones are almost certainly
      thin lifted code, so its share of all EDGE busy time is lower still, likely 15-20% (inferred, not measured). Main thread: 60%+ of its samples are in the SPU wait
      functions (`416338` 28.8, barrier `466E38` 19.4, `416610` 8.2, `464B0C` 4.0, `416A10` 3.1), `ps3_indirect_call` is
      0.9%, store coherency 1.3%. So the main thread has slack and a cheaper store path would not move the frame.
    - Four EDGE lanes (x380, runZ1, `SPURS_LANES=15,16,17=4;3,7=3` against the default three, warm-up then A B B A, 420 s,
      game frames/s 300-390 s): warm-up 17.02, A 14.10 / 16.14, B 12.02 / 15.02. Other programs averaged 0.5-3 cores, so
      that spread carries no frame-rate number; both pairs favoured three lanes and it is not pursued. x379 has the EDGE
      threads ~39% in wait, so more lanes finding no gain points at a latency-bound chain, not a throughput-bound one.
    - The policy module's wrapper around each EDGE job (x381, runZ2 = runZ1 as a lab build, `SPURS_PM_PROF=60`, 480 s,
      four windows): w15-17 73-78 us job proper of 95-102 us per module run, proper/module 77%, so the wrapper is 23%
      (22-24 us a job, ~22k jobs a second over the three lists). The run has no interrupt inside a job (they land in the
      module's own windows), so its stall-and-notify cost sits in that 23%. Channel writes are 17% of the workload's
      time, reads 2%, tag waits under 1%: the wrapper is work, not waiting. With x379's ~61% busy that is ~14% of EDGE
      thread wall time; x377 has the EDGE chain ~0.75 binding, so a native job loop has a ceiling near +10% and
      realistically +5-8% (inferred). General6 (wid 3) is 97% job proper at 1 ms a job, so that loop does nothing for it.
      Next: a native job loop inside the kernel threads, one EDGE workload first, gated, and compared inside one run.
    - Native job driver, first slice (toolkit `spurs_wws_native.c`, `SPURS_WWS_NATIVE=<wid>,...`, default off). C now drives
      the claim and the interrupt windows, one job a run; stage, load commands, the job, the store and the exit stay
      lifted, so every PPU-visible store but the claim's row is still the lift's. Anything it cannot prove returns to
      the lifted module, before or after the claim. x382 (wid 15, runZ3, 480 s): 95% of runs native, the rest list ends,
      nops and blocks, one lifted fallback class only; per job claim 1.0, stage 0.9, load 10.8, tail 6.0 us; w15 busy
      67-68% against 71% for w16 and w17 at equal job counts, ~4-5% less SPU time. x383 (all three lists): `SPURS_WWS_CHECK=1`
      420 s and six runs in all with 0 `CHECK FAILED`, no hang. In-run A/B at 3.3-5 cores of other load: `none` +1.0, `wws`
      +1.4 / +1.2 ms a frame, all inside the noise, and with the wrapper only ~4% down (worth at most ~2% of frame rate,
      inferred) no A/B here could see it. So no frame-rate claim: the driver stays off by default. x382 and x383 ran
      non-lab, so `[pm-jobt]` and the wid 16-17 coverage counters are still owed, on a quiet host.
    - What that slice showed: the claim and the interrupts were the cheap ~4 us of the wrapper. The ~17 us left is the lifted
      load commands (11 us) and store and tail (6 us). Next: native load commands, judged by phase us and per-list busy,
      not by frame rate. Under ~8 us of wrapper the native loop is on track; if the load's buffer rules cannot be made
      native without guessing, this path stops near here and the remaining lever is the job itself.
    - Native load commands (`SPURS_WWS_LOAD`, 2 = C load plus code reuse, the default under `SPURS_WWS_NATIVE`). What the
      11 us was: a job moves ~65 KB, 81% of it 53 KB of code in 4 transfers that the module's own reuse rule never skips,
      because with one job a run every job sits in slot 0 and the rule reads the empty previous slot. The C load follows
      the module's ops on a copy of the records, reuses a code buffer on the same EA, size and overlay when no resident
      slot lies in the part it does not fetch, and re-fetches the last 4 KB, the only part jobs were seen to write. Any
      op it does not know goes to the lifted load. x385 (runZ5, all three lists, 480 s): check mode 0 `CHECK FAILED` over about
      185k compared loads a minute per list (C first, lifted result stands, records, LS 0x4000-0x3DFFF, the resident table
      and overlay compared), no hang. Timing run: 95% native, reuse 99.4%, 16.4 KB and 14.8 transfers a job, per job claim
      1.1, stage 0.9, load 6.2 (3.9 of it transfers), tail 5.9 us, so ~14 us of wrapper against the 22-24 of x381. All
      three lists 59-60% busy at ~370k jobs a minute, ~97 us a job, against ~71% at ~400k (~106 us): about 8-9% less SPU time
      per job, inferred across runs at different load. The bound on what a job writes is measured, not proved; the
      check watches it. Frame rate not comparable, 4.8 cores of other load. Still off by default.
    - x386 (runZ5, all three lists, in-run A/B at 2-5 cores of other load): `none` totals -0.3% in frame time, `wws` totals
      1.4% and 2.1% faster, but block medians +1.7, +2.2, +1.5 ms a frame the other way. Heavy-tailed load noise, no
      frame-rate claim. `[pm-jobt]` no longer owed for the job itself: 102 us a job was 78 proper + 24 wrapper (x381),
      ~97 is ~83 + 14 (x385), where the driver's job phase also holds the call bracket, and the total fell by the wrapper's
      drop. Stays off by default until a quiet paired A/B (three load samples under 1.3 cores, warm-up then old new
      new old, 480 s) and one soak of 25-30 minutes, then a port default like `SPURS_LANES`.
    - Sampler on the driver (x387, runZ5, 420 s, 4000 buckets, ~66% of non-wait samples covered): EDGE threads lifted 38%,
      native 26%, other 10%, dispatch 9%, channels/MFC 8%, SPU other 5%, coherency 2%, overlay 1%, against dispatch 15%
      and channels/MFC 11% in x379. `spu_indirect_branch_slow` fell from 3.7% to 1.2%. The module's lifted code is 2.7% of
      an EDGE thread, so a native tail buys little. Lifted job code left is flat: d935 11.3%, 1bdd 6.1%, baef 5.5%, no
      single function over 1.7%. The run was at 10.6 game frames/s under heavy other load, so the wait shares are
      not comparable with x379.
    - A metric the host cannot blur: `GOW_AB` now times the EDGE module runs per window (`g_spurs_ab_*`, `us_run=`, and how many
      were native, which also proves the lever toggled). x388 (runZ6, 5 s windows, 300-680 s, ~3.4 cores of other
      load): `none` 89.2 / 88.9 us a run, block difference +0.6 (quartiles -1.5 / +1.0). `wws` 89.2 us on against 98.9 off, native
      share 94% / 0%, block difference -8.6 us (quartiles -10.8 / -7.2), 19 of 19 blocks. So the driver takes 9.8% off an
      EDGE module run, measured inside one run. Frame time in the same run stays unreadable (quartiles -15 / +14 ms).
    - Soak (x389, runZ6, all three lists, 1771 s, ~4 cores of other load): no stall, 94% native throughout, 11.4-11.6 game
      frames/s from 300 s and 10.9 in the last window. Dark frames end at the load screen (last at frame 10800; the
      baseline's last is 13500), bright frames after that, spot-checked at three points. Check mode was not on in
      this run; its evidence is x385's full 480 s. The driver's default waits on a quiet same-exe pair (x390).
    - Same-exe pair (x390, runZ6, warm-up then old new new old, 480 s, after three load samples under 1.3 cores; arms still
      started at 0.9-2.7 cores of other load): 90.5 / 81.1 / 80.1 / 90.9 us a module run (-11%), 96% native / 0%, and 14.07 /
      14.39 / 15.23 / 13.71 game frames/s from 300 s (the first two logs end at 450 s), old 13.9 against new 14.8, +6.6%. Both
      pairs point the same way (+2% and +11%), but thirds of one arm range from 12.9 to 15.1, so the size is not pinned. A
      10% cut with the EDGE chain ~0.75 binding (x377) predicts about 7%. So the driver is now the port's default
      (`SPURS_WWS_NATIVE=15,16,17`, runZ7; `SPURS_WWS_NATIVE=off` goes back).
    - Causal probes on the driver (x391, runZ7, 50% probes, O E G G E O, 480 s, game frames/s 360-480 s, 95-96% native in all
      arms): O 16.79 / 13.77, E (EDGE wids 15-17) 14.05 / 13.02, G (General6) 14.73 / 14.73. O's arms differ by 22%, so the raw
      figures are loose: E -12% (adjacent pairs -16% / -5%), G -4%. Within every arm frames/s follows `us_run=` by 5 s
      window (r -0.8 to -0.95, pooled O -0.96), which tracks interference that hits everything, not EDGE's own worth: the slope is
      an elasticity near -1.6, more than a binding stage can give, so it is a covariate only and predicts no gain. `us_run=`
      leaves the probe's wait out (E1 72.6, O1 72.7). Compared with O at the same `us_run=`: E -18% / -19%, G -3% / -6%; the E
      probe spins about a core more, so its `us_run=` may be inflated and -18% understated. Against -33% for a fully binding
      stage (linear ratio) EDGE is about half and General6 small. It stays the largest single binder; x377 had it at
      about three quarters on one arm of the old build, inside this noise, so no claim that the share fell. The O1
      arm is the best figure so far: quiet ~17 game frames/s, loaded ~13.8-15.
    - The walker binds again (x392, runZ7, `RSX_SLOW_PCT=50`, O R R O, 480 s, game frames/s 360-450 s, no reap): O 15.00 / 14.24, R
      12.73 / 13.68, -9.7%, adjacent pairs -15% and -4%; O2 ran at 4.3 cores of other load against 2.9 for R2, so the
      second pair understates. Likely -10 to -15%, about 0.3-0.45 of a binding stage. x376 had a 25% walker probe helping
      by 8%, so the sign flipped: the SPU side got faster and the two chains are near balance, which fits an 11% EDGE cut buying
      about 6%. The probe also holds the FIFO lock while it spins. The `us_run=` covariate fails here: the R arms read 74.5 /
      73.6 against 80.5 / 82.7 with load rising, because a slower walker means fewer concurrent module runs, so the
      regression's -24% is not a result. It only serves a probe that leaves `us_run=` alone. That also weakens x391's adjusted E
      figure (E2 76.5 against O2 84.5), so read E as -12% to -18%. Parked walker levers are open again. Next: an in-run walker
      metric like `us_run=`, since frame time cannot resolve a walker lever.
    - A walker metric inside a run (x393, runZ8, toolkit `g_gcm_ab_*`, `GOW_AB=none`, 480 s, with the host sampler, ~2.4 cores of other
      load, 13.7 game frames/s): `[ab]` now prints `draws=`, `us_draw=` (time in FIFO passes per guest draw, lock wait and probe
      spin left out) and `rsx_busy=`. 27.2 us a draw, quartiles 26.2-28.3, walker busy 0.89 of a core, ~2470 draws a frame, so
      about 67 of a 73 ms frame. Block difference of `us_draw=` under `none`: 0.6 us of 27 by window medians. Frames/s follows it by
      5 s window at r -0.97, partly an identity with busy and draws a frame both nearly flat, so it is a readout, not proof.
      Walker samples from 300 s: ntdll 18%; exe 51% (top buckets only): vertex fetch and remap about 19, `gcm_rsx_process_fifo_unlocked`
      with dispatch and the engine's method hook about 15, end of draw 4, textures about 4; the other 31% are drivers or
      buckets outside the top 4000.
    - x393's unexplained 31% (x394, runZ9, sampler per thread and module, `passes=` and `words=`, 480 s, 13.5 game frames/s): the walker is
      exe 60%, ntdll 19%, `nvoglv64` 10%, `VCRUNTIME140` (memcpy) 7%, `win32u` 3%. So the driver is about 13%, not the 31%. 34 FIFO words a
      draw, 83k a frame, 153 passes/s, 215 draws a pass, so no spinning on empty passes. The FIFO block at about 15% of 27 us is ~118 ns a
      word. No heap per draw (`free(d)` in the draw path is a debug branch) and `getenv` per pass is too rare to show (ucrtbase 0.6%).
    - FIFO argument fast path (x395, runZa, toolkit `g_gcm_fast_args`, default on, `GCM_FAST_ARGS=0` off, in-run lever `fifo`): a run of plain
      methods goes straight to the draw engine without the page lookup and debug gates per word; a method with an effect of its own, a run
      crossing an IO page or any debug gate takes the full loop. `none` arm, sampler off: block difference of `us_draw=` 0.00, quartiles -0.46 /
      +0.24, `rsx_busy=` +0.006. `fifo`: 19.4 against 20.8 and 23.0 against 23.9 us a draw by window medians (-6.6%, -3.7%), `rsx_busy=` 0.80
      against 0.84 in both. Frames flat: 16.45 / 16.34 and 14.37 / 14.34 game frames/s, `none` 14.77 / 14.70. Frames after the load
      screen look like the `none` arm; dumps have the same brightness. By the read rule that is less walker work in a chain not holding
      the frame at that moment. Kept on: it costs nothing and leaves the walker more headroom for loaded hosts.
    - Two corrections from that sampler-off arm. The 1 ms sampler inflates the walker: 22.2 us a draw and busy 0.81 without it against 27.2 and
      0.89 in x393/x394, so "67 of a 73 ms frame" overstates it; without the sampler it is about 54 of 68 ms. And x392 does not show the walker
      binding now. With busy ~0.81, a 50% probe asks for 1.2 of a core, so frames fall by up to ~18% even if the walker had slack; x376's 25%
      probe landed about at 1.0, which fits no loss there. What x392 measures is a walker with roughly 15-20% headroom, and `fifo` moving
      `us_draw=` with frames flat agrees. The same reasoning limits x391's probes, which need each chain's utilisation to read.
    - Critical path on the driver (x396, runZa, `PS3_FTRACE` 20 s from 300 s, sampler off, ~1.7 cores of other load): 312 frames, median 61.6 ms.
      Five SPUs 52.4 ms busy (85%), the sixth 33.2 (54%), walker drain 53.3 (86%), main thread blocked 3.8. x315 on the old build: 67.1 ms, SPUs
      ~61 (90%), walker 61.6 (92%). Work per frame fell 13-14% on both sides, the frame 8%, so the idle share grew from ~8% to ~14%, which
      explains x390 and x395. This replaces the inferred headroom above with a direct number. Balanced SPUs would need 49.2 ms and the walker
      53.3, so neither is a hard ceiling yet (18.8 game frames/s for the walker). The idle is not a common stop: 4 or more SPUs and the walker
      run together for 49 of 64 ms, and 2 or fewer SPUs with the walker idle is 1.6 ms a frame. Walker gaps total 10.6 ms a frame across 23
      passes, grouped mid-frame and after the flip, with no fixed quantum (the 4 ms `Sleep` bucket is 1.3 ms a frame). SPUs 0-4 lose ~11 ms each
      over ~7 waits, around wids 3 and 15-17 (SPU 2 around wid 8). So the two chains take turns waiting on each other, in small pieces.
      With about 8 busy threads on six physical cores (i7-8700K) and the main thread ~60% in its polls, the next question is contention.
    - Spin hint again, with a metric that resolves it (x397, runZb, in-run lever `pause`, `GOW_AB_PAUSE=16` and 128): block differences 16 pauses, `us_run=`
      +0.51 (quartiles -1.97 / +1.16), `us_draw=` +0.05 (-1.23 / +1.68); 128 pauses, `us_run=` +0.57 (-0.87 / +2.87), `us_draw=` +0.45 (+0.12 / +1.42), 8 of 9
      blocks slower. Totals 16.47 / 16.67 and 16.68 / 16.91 game frames/s, on then off. The polls do not cost their siblings anything readable
      and a long pause costs a little, so x318's neutral stands and `PPU_SPIN_PAUSE` stays 0. These arms ran at 0.8-2.8 cores of other load and are
      the best sustained figures so far, 16.5-16.9 game frames/s.
    - The kernel threads' pause, again (x398, runZc, in-run lever `nap`, toolkit `g_spurs_nap_us`, `GOW_AB_NAP=200` and 50): SPU gaps in x396 have a floor at 1 ms,
      nothing between 0.2 and 1, because of `Sleep(1)` (~1.5 ms), so a job ready just after an empty pass could wait that long. A high-resolution pause
      does not help: 15.80 / 16.06 and 15.98 / 15.73 game frames/s, on then off, `us_draw=` +0.9 and +1.4, `us_run=` +4.1 and +3.9 (about 5%). EDGE module runs per window also fell 2.5% and 5.9%. So
      kernel-side wakeup latency is not the lever and the default stays `Sleep(1)`. The test cannot say whether those gaps have work ready: a gain could be hidden by the
      added contention.
    - The walker's idle wait (x399, runZd, in-run lever `idle`): x396's walker gaps are not all small: ~0.7 gaps of about 8 ms a frame, 5.9 ms a frame, clustered at 7-8 ms and
      at 4 ms, one or two of the port loop's `Sleep(4)`. `GOW_IDLE_WAIT=2` (`Sleep(1)`) against 0: 14.42 / 13.79 game frames/s (+4.6%), 3.2 ms a frame less, frames/s up in 7 of 8
      blocks, block median +0.80 (quartiles +0.37 / +1.07; `none` +0.10, -0.18 / +0.40), `us_draw=` -2.6, EDGE module runs up 5.8% a window, at ~4.8 cores of other load.
      `GOW_IDLE_WAIT=1` (kick wait): 15.08 / 15.17, flat.
      `SPURS_NAP_READY=60` (x399p): of passes after a pause, wid 8 is ready and not running in 15-19%, wid 3 in 10-11%, wid 10 in 7%, EDGE lists under 3%. Wid 31 reads 100%, an
      out-of-range slot of the counter, ignore it. Those shares counted every thread, also those that may never run a workload (wid 8,
      BrBDblBufWrkld, has priority on SPU 5 only; the ftrace row 2 in x396 is that SPU's kernel thread, not SPU 2), so they overstate; x402 has the gated figures.
    - x399's gain did not repeat. Five more arms of `GOW_IDLE_WAIT=2` against 0 on runZd (x400, 1.5-2.3 cores of other load; x401, the same plus `burn.py 3` for the arm):
      totals on / off 14.23 / 14.35, 14.23 / 14.45, 17.49 / 17.25, 14.39 / 14.11, 13.31 / 13.41 game frames/s; block medians -0.10, -0.60, +0.30, 0.00, -0.19 (`none` +0.10).
      So x399i2 was the tail of the spread and the default stays `Sleep(4)`. The walker's 8 ms gaps are then not the sleep's length but no commands yet, as the
      gap after the flip suggested. Three extra burning cores cost little here: x401's arms average 14.25 / 13.36 against 14.3 for x400a/b, so the loaded
      column now holds within the run-to-run spread.
    - Who the walker waits for at the end of a frame (x396 and x402, runZe = runZd + a park flag on the trace's DRAIN spans, `[main-prof]` split by the last quarter
      of a game frame, `SPURS_NAP_READY` gated on each thread's priorities; 420 s, ftrace 20 s from 300 s, 13.8 game frames/s at the time):
      - The walker's gaps of 3 ms or more (9.3 ms a frame in x402) are a block late in the frame, after it reached the frame's flip; their end time is flat modulo
        16 ms, so no vblank pacing. 95% of passes end on a self-JUMP park.
      - In the last quarter of a frame the main thread spends 44% of its samples at `0x467B2C` in `0x3B35B4`, the HighestPrio6 (wid 2) completion wait, and is not
        blocked. HighestPrio6 runs at 91-94% of the frame, 1.46 ms a cluster, 1.25 clusters a frame, and in 60% of clusters it starts within 0.3 ms of a
        BrBDblBufWrkld (wid 8) run ending (EDGE lists ~31%, RenJobSet 11 21%). From its end to the next guest flip request is a median 3.3 ms.
      - Correction, same day: the 60% is wid 8's base rate, not a link. Against random instants in the same 85-97% phase window, a wid 8 run ends within 0.3 ms
        of 62% of them (lift 0.96 in x402, 1.04 in x396), and the EDGE lists sit at their base rates too. wid 8 is the audio workload, a continuous consumer.
        What does stand out: ModelServer (wid 5) ends within 0.3 ms of 21% of HighestPrio6 starts against 4% at random (lift 5.6 and 4.7 in the two traces), and
        RenJobSet (wid 11) has stopped for the next 1 ms in 93% of them against ~29% at random, so it finishes shortly before. wid 5 is 0.5 ms a frame, wid 11
        13 ms; both are single-lane (`maxC=1`), on SPUs 0-4 and 0-5. The frame end is RenJobSet / ModelServer -> HighestPrio6 -> main thread -> commands.
      - `SPURS_NAP_READY` with the priority gate: wid 3 9-12%, wid 8 7-8%, wid 10 7%, others under 5%.
    - (Measured before the correction above, so wid 8 is not the frame's gate.) Where wid 8 spends its time (x403, runZe, host sampler on threads running wid 8 only, 480 s, 14.8 game frames/s): job code ~60% (jobs `5bc2fd` 25%,
      `3105a3` 20%, `c5a3be` 12%, `d3eebe` 4%, no function over 4.7%), dispatch ~16% (`spu_indirect_branch` 7.7, its slow path 2.3, `spu_drain_call_loop` 3.1),
      `VCRUNTIME140` memcpy 8.5%, channels/MFC 5.7%, the lifted policy module 5.6%. A wid 8 module run takes ~189 us (x404l), against ~80 for an EDGE run.
    - The native driver does not take wid 8 (x404, runZf, port lever `wws8`): check arm `SPURS_WWS_NATIVE=8,15,16,17 SPURS_WWS_CHECK=1`, 300 s: no CHECK FAILED,
      EDGE lists 95% native, wid 8 0% native, every run declined as `shape`. Its workload data is `0x847D3480009FDF80`; the driver requires the high word 0
      (the EDGE lists' is), and what the module does with that word is not known yet. The lever arm then toggled nothing: 13.99 / 14.18 game frames/s.
    - More lanes for the lists before HighestPrio6 (x405, x406, runZg, toolkit `g_spurs_lane_ovr`, in-run lever `lanes`, `GOW_AB_LANES=<wid>=<n>,...`): RenJobSet
      alone on 2 lanes 13.11 / 13.11 game frames/s, on then off. RenJobSet and ModelServer on 2: 13.38 / 13.09, faster in 9 of 9 blocks. ModelServer alone on 2,
      twice: 12.94 / 12.68 (7 of 9) and 13.13 / 13.31 (3 of 9, that arm started at ~6 cores of other load). Two of three arms near +2%, one -1.3%: not a result
      at this resolution and no default. No stall and no wrong frames with either list on two lanes.
      A lever that shortens the frame's tail would also raise `rsx_busy=` with `us_draw=` flat; x405b read +0.002 and -0.77, x406a flat, so the 9 of 9 was spread.
    - The start of a frame (x402): in the 5 ms after the guest's flip request 4.9 of 6 SPUs are busy (quartiles 4.7-5.2), on BatchJobList 24%, the EDGE lists 44%,
      RenderJobs 18%, wid 8 13%. The walker's first drain comes a median 5.9 ms after the request, and that pass ends on an EDGE park in 71% of frames. So the
      frame opens with the SPU side full and the walker waiting for EDGE output: what sets the pace there is SPU job code, not scheduling or any host wait,
      which matches every wait-side lever coming out flat (x397-x401, x405-x406).
    - Generic SPU-side costs on the current build (x407, runZg, host sampler on every SPURS kernel thread, 480 s, 4000 buckets, 12.9 game frames/s): runtime
      helpers ~18% of kernel-thread time (dispatch 7.2: `spu_indirect_branch` 2.6 and its slow path 0.6, `spu_drain_call` 1.1 and its loop 0.9, `spu_lookup` 1.1 and
      its walk 0.9; channels/MFC 4.7; coherency 1.1; the interpreter's `spu_step` 0.4), `VCRUNTIME140` memcpy 6.3%, lifted and native job code ~74%. The top
      buckets cover only 37% of samples and favour compact runtime functions, so job code is the rest by construction, not the 11% the covered buckets show.
      The memcpy is DMA of data another core just wrote (~49 KB over ~24 transfers an EDGE job), cache misses rather than call cost.
    - The indirect-branch cache (x408, runZg, `SPU_IBC_COUNT=1`, 300 s each, ~5M dispatches/s): 512 entries a thread (the default) hit 92.1%, miss 7.2%, refill
      4.7%; 4096 entries hit 96.0%, miss 3.3%, refill 0.8%. In-run (x409, runZh, toolkit `spu_ibc_set_size`, lever `ibc` 4096 against 512): `us_run=` block
      differences -0.49 (quartiles -0.79 / -0.17, 8 of 9) and -0.69 (-1.95 / +0.01) against the same exe's `none` -0.30 (-1.31 / +0.05). Under 1% of an EDGE
      module run and frames flat (13.24 / 13.31, 14.09 / 13.86): halving the misses buys little because a miss is cheap next to the job. Default stays 512.
    - Where that leaves the SPU side: what a generic runtime change can still take is a handful of items of 1-7% of SPU time each (dispatch, channels,
      coherency), each worth ~1-3% of frame rate at best. The large share is the lifted job code itself.
    - The native driver on the other job lists (x410, runZh, check mode, `SPURS_WWS_NATIVE=2,3,7,9,10,11,12,13,15,16,17 SPURS_WWS_CHECK=1`, 300 s): every list
      but wid 8 has a zero high word in its workload data and the same policy module. No CHECK FAILED. Native share per list: HighestPrio6 9%, General6 48%,
      BatchJobList 27%, RenJobSet 69 / 53 / 73 / 10% (wids 9-12), RenderJobs 51%; most of the rest are `end` runs (an empty list, which go to lifted code). No
      code reuse on those lists, a load of 24-53 us a job.
    - In-run (x411, runZi, port lever `wwsx` on wids 2,3,7,9-13 with the EDGE lists on throughout, `us_run=` timing those lists): native share 0% -> 55-57%.
      Game frames/s 14.49 / 13.93 (+4.0%, faster in 8 of 9 blocks, block median +0.40, quartiles +0.20 / +0.90) and 13.48 / 13.14 (+2.6%, 6 of 9), against the
      same exe's `none` -0.05 (-0.47 / +0.23). Their module runs stay ~170 us either way (most of a run is job code; the empty runs are lifted in both arms).
      All three arms ran at 4.4-5.3 cores of other load. A candidate: soak with check mode next, then a default.
    - Soak (x412, runZi, all eleven lists native, `SPURS_WWS_CHECK=1`, 1771 s, masher throughout): no CHECK FAILED, no stall, 11.70 game frames/s from 300 s
      (11.89 / 11.74 / 11.65 at 600, 1200 and 1711 s; check mode runs every other run twice), native shares as in x410, dark frames end at the load screen
      (last at 13500, as the baseline). The port default is now `SPURS_WWS_NATIVE=2,3,7,9,10,11,12,13,15,16,17` (runZj); `off` goes back, wid 8 stays lifted.
    - Confirmed on the default build (x413, runZj, lever `wwsx` taking those lists off and on, 4.2-4.7 cores of other load): 12.28 / 11.86 (+3.5%, 7 of 8
      blocks, block median +0.40, quartiles +0.19 / +0.87) and 15.69 / 15.55 (+0.9%, 6 of 9, +0.30). Four arms in all: +4.0, +2.6, +3.5, +0.9%, every
      block median +0.3 to +0.4 against a `none` floor of -0.05.
      Their module runs not moving while frames rise fits a shorter chain rather than less CPU (inferred): RenJobSet is single-lane on the frame-end chain,
      ~150 jobs a frame, so ~10 us less wrapper a job is ~1.5-2 ms of a 64 ms frame. The empty `end` runs those lists leave lifted are not worth taking:
      ~860 a second at under 25 us each is about 2% of one thread.
    - Code reuse for the other lists (x414, runZk, toolkit `g_spurs_wws_reuse_any`, `SPURS_WWS_REUSE_ANY=1`, in-run lever `reuse`): the driver reused only EDGE's 0xD000
      code buffer, so the other lists reloaded theirs every job. Check arm (300 s, `SPURS_WWS_CHECK=1`): no CHECK FAILED; reuse on 99% of native jobs for RenJobSet
      (wids 9-11) and RenderJobs, 23% for General6, 73% for BatchJobList; load per job only 24 -> 21 us (wid 9), 26 -> 22 (wid 10), 53 -> 41 (wid 13), so most
      of their load is data, not code. Lever arm (480 s): 14.94 / 14.80 game frames/s, block median +0.40 (quartiles -0.40 / +0.45, 5 of 9), `us_run=` -0.56
      (-3.94 / +1.65): inside the spread. Stays off by default.
    - How tight the lifted SPU code is (no run; the hottest wid 8 job function in x403, `jobmod_5bc2fdee17f4_e11a08_spu_func_00015400`, clang-cl /O2, `llvm-objdump`):
      ~355 SPU operations become 3596 host instructions, ~10 a SPU op. 1178 of them address the stack (525 stores, 213 loads: 128-bit values spilled), 484 the
      context (`ctx->gpr` round trips, ~1.4 a SPU op), 47 calls. The lifter writes every result to `ctx->gpr[n]`, and `ctx->ls` is a `uint8_t*`, so any local
      store write may alias the registers as far as the compiler knows. That is where the 74% of SPU time in job code goes, and the generic lever is in the
      lifter's output (registers in locals across a block, no aliasing between local store and the register file), a larger piece of work to plan on the
      job-replay harness rather than in-game A/Bs.
    - Quiet column for the current defaults (x415, runZk, native driver on eleven lists and the FIFO fast path, `GOW_AB=none`, two 480 s arms
      started 2026-11-08 00:51 and 00:59 after three load samples under 1.3 cores, at 0.68 and 0.26 cores of other load; both logs end at 450 s):
      18.41 and 16.45 game frames/s over 300-450 s. Each arm is steady inside itself (18.63 / 18.00 and 16.40 / 16.40 over 300-360 and 390-450 s),
      so the 11% between them is run to run, not drift. Against the best quiet figures before these defaults (x397, 16.5-16.9) that is level to +9%, but x397 ran at 0.8-2.8 cores of other load against 0.26-0.68 here, so part of it is the quieter host (the lever's own size is x411's in-run A/B);
      the last loaded figures with the driver on those lists were 13.5-14.5 (x411, 4.4-5.3 cores of other load).
      Loaded column for the same build (x416, `burn.py 3` for each arm on top of 1.2-1.7 cores of other load, 01:13 and 01:21): 13.94 and 14.16 game frames/s
      over 300 s to the end (450 / 480 s), against x401's 13.3-14.4 on runZd (driver on the EDGE lists only) under the same load. Under load the extra lists
      hold level with that rather than gaining. The loaded condition as a whole (~4-5 busy cores besides the game) takes ~15-25% off the quiet figure;
      x401 against x400 showed the burners add little to ordinary 1.5-2.3 cores of other load.
    - Codegen across all lifted modules (no run, production objects, `llvm-objdump`): 2718 functions of 50 SPU ops or more average 7.7 host
      instructions a SPU op (median 7.2, p90 14.3), 18% of them stack accesses; the hottest wid 8 function (10.1, 33%) is in the upper part, not an
      outlier. The job-lab harness still holds on the current tree (lab exe rebuilt 2026-11-08: `work/jobcap4` 400/400 and `work/jobcapA67` 300/300
      identical, REPEAT=20 medians 0.054 and 0.699 ms a job). The plan for registers in locals is docs/LIFTER_CODEGEN.md.
      Its pilot (phase 1 applied by script to a67e's 4930 and 4AB0, lab exe, `jobcapA67` on the lifted noise path, REPEAT=20, base/pilot/pilot/base):
      300/300 identical, sums of per-job minimums 460.9 / 474.0 / 470.0 / 480.3 ms, flat. On the hot wid 8 function the context references fell 569 -> 176 but
      stack references rose 1193 -> 1316: lifted functions name a median of 70 SPU registers against 16 host vector registers, so values spill either way.
      The cost is register count, not aliasing; the plan is not recommended in this form.
    - What the remaining host instructions are (no run, the hottest wid 8 function compiled with `/FA` and DWARF line tables, each instruction charged to
      the helper its line is in): of 3593, `spu_shufb` ~630 (84 ops, ~7.5 each), the float ops ~1300 (~57 ops, mostly the fast-path check around a single
      `vfmadd`/`vaddps`/`vmulps`), LS access ~280, lifted glue (branches, address math, register moves) ~790.
    - Cheaper `shufb` and float fast-path check (toolkit 93819cd1, `runtime/spu/spu_helpers.h`):
      - `shufb` takes 9 vector ops instead of 12. The index is `C ^ 3` with no mask, A or B is chosen on `C << 3`, and the special selectors come from one
        `pshufb` lookup on the top nibble, blended on C's bit 7.
      - Under `SPU_FP_DAZ` (FTZ and DAZ set by `spu_run_with_halt`), fa/fs/fm/fma/fnms/fms flag only exponents 254 and 255 (`(r & 0x7F000000) == 0x7F000000`)
        and turn -0 into +0 with one compare. Without `SPU_FP_DAZ` the old check stays.
      - Old against new header, bitwise, with that MXCSR: 4M `shufb` cases (every control byte in every position) and 20M vectors for each float op, 0
        differ. A deliberately broken variant gave millions of differences.
      - Job replay (lab exes, REPEAT=20, ABBA, sum of per-job minimums, all identical):

        | Capture | Old | New |
        |---|---|---|
        | `jobcapA67`, default | 210.8 / 209.8 ms | 175.7 / 159.3 ms |
        | `jobcapA67`, lifted noise path | 522.4 / 460.7 ms | 387.4 / 389.1 ms |
        | `jobcap4` (EDGE, mostly native) | 26.3 / 27.4 ms | 25.6 / 25.7 ms |

      - In game (x417 + x418, runZk against runZl, 480 s, quiet host, game frames/s from 300 s): runZk 16.33 / 16.66 / 16.05, runZl 16.72 / 16.83, so +2.6%,
        and both runZl arms are above all three runZk arms.
      - Loaded (x419, `burn.py 3` for each arm, order Zk Zl Zl Zk, 480 s): runZk 13.99 / 13.99, runZl 15.03 / 14.58 game frames/s from 300 s, so +5.8%.
        Under load the SPU threads compete for cores, so cheaper job code pays more.
      - x417's third arm (runZl, 13.29) is left out: its walker took 25.9 us a draw against 19.3-20.6 in every other arm, and the walker does not use these
        helpers, so the host was busy.
      - The frame gain is small next to the job gain because the EDGE lists, the bulk of the frame's SPU work, are 96% native; the helpers speed up the
        lifted job code. Now the default (runZl).
    - Where the critical lists spend their SPU time (x420, runZl, host sampler restricted to a group of wids, 360 s each; scratchpad `sampres.py` resolves
      the buckets with the exe's map):
      - Frame end (wids 2, 5, 9-12): ~60% of the top-bucket samples are in lifted job code (`jobmod_a00f81d5483b` 20.8%, `7fc1da1d2ec7` 13.7%,
        `98f3fc98b30c` 9.8%, `8ae46d2d2727` 9.8%, `e0194f1d9c80` 3.5%).
      - Frame start (wids 7, 13): `jobmod_baef994426b2` 46%, `759dedc8269b` 7%, and the interpreter ~9% (`spu_step`, `spu_interp_run_until_impl`)
        with another ~12% in lookups.
    - The interpreter time (x421, `SPU_INTERP_MISS=1`, 360 s): 2.2-3.8 s interpreted per 20 s, almost all BatchJobList (wid 7) running copies of
      the four relocatable EBOOT blobs.
      - These copies had no resident slot. A blob becomes resident only when a GET of its source lands, and these copies got into local store
        another way: moved, partial, or a placement whose slot a later GET of the same image took over.
      - The largest single target, LS 0x359C0, is a partial copy of blob 0x6D66B0 from +0x640. No GET of wid 7 ever covers it (x422, x423,
        `SPU_LSA_GETLOG`), and it alone took ~1.9 s per 20 s.
      - Not the native WWS driver: with the driver off on wid 7, the time is the same (x424).
    - Relocatable copies adopted by content (toolkit 6b7f39e3, `spu_reloc_adopt` at the top of `spu_smc_microstep`, `SPU_RELOC_ADOPT=0` turns it off):
      - On an unresolved target the runtime looks for the bytes at pc inside each relocatable region's source.
      - The intact quadword run around pc (at least 256 bytes) becomes resident with the matching canonical offset, so lifted code runs only where
        its bytes are the ones it was lifted from.
      - It dispatches again when the canonical lift has an entry at pc. Otherwise the interpreter runs on and rejoins the lift at its next entry.
      - A negative cache keyed by (pc, first two words) keeps the scan to once per foreign target.
      - The first version returned "dispatch again" for a target that was no lift entry and adopted the same copy forever: a pm-hang at wid 7,
        fixed before any measurement.
      - Results:
        - x425: interpreted time 12-138 ms per 20 s (from 2.2-3.8 s), no pm-hang.
        - x426, quiet, 480 s ABBA: runZl 16.17 / 16.26, runZm 16.23 / 16.27 game frames/s.
        - x427, loaded, `burn.py 3`: runZl 14.93 / 14.56, runZm 14.39 / 14.50. Inside the spread; arm c started at 4.95 cores of other load.
        - No pm-hang in nine runs.
      - It frees ~14% of one core that the interpreter used. That is not frame time, because BatchJobList is not what the frame waits on.
      - Kept on (runZm is the default).
    - Which job modules the frame-end lists run (x428, lab exe, `SPU_JOB_CAPTURE` on wids 2, 5, 9-12, 240 jobs, one in 40 from 150 s, `work/jobcapF`;
      game data, not committed):

      | Wid | List | Module |
      |---|---|---|
      | 9 | RenJobSet | `98f3fc98b30c` |
      | 10 | RenJobSet | `8ae46d2d2727` |
      | 11 | RenJobSet | `a00f81d5483b` |
      | 12 | RenJobSet | `7fc1da1d2ec7` |
      | 2 | HighestPrio6 | `e0194f1d9c80` |
      | 5 | ModelServer | `e9f3f00d551e` |

      - The 102 wid 11 jobs replay identical (sum of per-job minimums 12.2 ms).
      - a00f's time is spread across many functions; its hottest, B708, is 11% of it.
    - Which SPU group paces the frame (x429-x430, runZn). Method:
      - The in-run lever `slow` turns on `SPU_SLOW_PCT` for the wids in `SPU_SLOW_WIDS` (`GOW_AB_SLOW=20`: each of their module runs busy-waits 20%
        of its time). Toolkit 4c3fef6f made the probe settable at run time.
      - 480 s per group, 5 s windows from 300 s.
      - A group that fully paced the frame would add ~16.7% to frame time.

      | Group slowed 20% | On-off ms/frame, block median (quartiles) | Frame time, on against off |
      |---|---|---|
      | none (floor) | +0.97 (-5.24 / +4.38) | -0.7% |
      | RenJobSet 9-12 | +4.98 (-9.11 / +7.78) | +0.9% |
      | EDGE 15-17 | +7.30 (-7.52 / +10.27) | +4.1% |
      | HighestPrio6 + ModelServer 2, 5 | +2.70 (-14.42 / +4.64) | -2.0% |
      | BrBDblBufWrkld (audio) 8 | +4.65 (-5.46 / +7.67) | +1.2% |

      - No single SPU group paces the frame on the current build. EDGE couples most, at about a quarter of full pacing. So per-module SPU work
        (a native B708, channel inlining on RenJobSet) is closed for frame rate. The next place to look is the PPU side: the main thread and
        the walker.
      - The interpreter time stayed at ~120 ms per 20 s over a full 480 s run (x429b).
    - One freeze (x430b, the arm with `slow` on wids 7 and 13, runZn):
      - Flips stopped at ~310 s, ten seconds after the lever began; the SPUs kept polling and there was no pm-hang.
      - `[GSTACK:stall-main]`: lr 0x3E8388, chain `func_003E82A8+0xE0` <- `func_003C6450+0x5C` <- `func_0033A0F0+0x2EC`, r3 = 0x1E. The walker
        had no new work for 31.5 s.
      - Not reproduced in two more runs of the same arm (x431, with and without `SPU_RELOC_ADOPT`). It is the only frozen run of ~30 today.
      - Left open with these details for when it recurs.
      - Three later freezes, with the same stack, were the walker probe's own bug: `g_rsx_slow_pct` was read twice per pass, so a pass that began
        with the lever off took no start time and then spun on garbage while holding the FIFO lock. They were x432a, x433a and x434a, the last
        with `SPU_RELOC_ADOPT=0`. Fixed in toolkit 8e8f4c05, and x435 ran clean.
      - The x430b freeze used the SPU probe, whose start time is always taken, so it is still unexplained.
      - Toolkit 8e8f4c05 also checks a relocatable placement's 16 bytes at the target against its source before running it, and drops the
        placement if they differ. No drop in 480 s, so stale placements were not the cause; it stays as a guard.
    - The PPU side (x432b, x435, in-run, 480 s each):

      | Probe | On-off ms/frame, block median (quartiles) | Frame time, on against off | Share of full pacing |
      |---|---|---|---|
      | Main thread: the thread that requests the flip busy-waits 3 ms a flip (lever `mslow`, toolkit `g_gcm_main_slow_us`) | +10.33 (-8.24 / +13.93) | 68.76 -> 71.04 ms, +2.3 ms of the 3 | ~75% |
      | Walker passes +20% (lever `rslow`) | +7.41 (-9.81 / +15.54) | 76.71 -> 80.75 ms, +5.3% | ~30% |

      - So the game's main thread is what paces the frame now. The walker and the EDGE chain (~25%, x430a) follow well behind.
      - The next lever is main-thread work.
    - Where the main thread's time goes (x436a `PS3_POLLTOP`, x437 host sampler on every thread, runZp, 360 s):
      - It sleeps only ~0.3-0.5 s per 60 s, so it is busy.
      - ~72% of its samples are its job-list polls: `func_00416338` 34% (the job-slot scan), `func_00466E38` 25% (the HighestPrio6 progress
        counters, from M0.7x), `func_00416610` 13%.
      - So the frame waits on lists through the main thread, and the `mslow` share is that chain.
      - Slowing lists 0, 1, 3, 4 and 6 together by 20% added +5.3% to frame time, slower in 8 of 9 blocks (x436b). That is the most consistent
        SPU signal so far.
    - A regression the sampler found in the adoption (x437-x440):
      - On runZp, `spu_indirect_branch_slow` took 37% of the audio kernel thread and 4-13% of the others. On runZl, before adoption, it was
        1-1.6% (x438).
      - With the adopt path noinline (x439), the cost was `spu_reloc_adopt` itself: 30.6% of that thread and 3-9% of the others. It scanned
        ~37 KB of relocatable sources for every unresolved target that was no copy.
      - Toolkit d43b1246 looks the bytes up in a hashed index of the sources' words (8727) instead. After (x440): no `spu_reloc_adopt` in any
        thread's top 30, the slow path ~1%, interpreted time unchanged at ~130 ms per 20 s.
      - So x426/x427 compared runZl against a build that paid this cost. The quiet and loaded A/B is redone on runZr.
    - One list at a time (x443, runZr, in-run `slow` 20% on a single wid, 480 s per arm, 2.8-8.6 cores of other load):

      | wid | On-off ms/frame, block median (quartiles) | Frame time, on against off | Lever faster in |
      |---|---|---|---|
      | 3 | -1.21 (-17.14 / +4.16) | 70.13 against 74.91 ms | 4/8 |
      | 1 | +1.03 (-7.41 / +5.96) | 64.11 against 64.48 ms | 3/9 |
      | 0 | +1.74 (-7.79 / +4.92) | 70.93 against 71.67 ms | 3/9 |
      | 4 | -0.68 (-10.73 / +2.92) | 81.93 against 83.59 ms | 5/9 |
      | 6 | +4.43 (-8.64 / +8.83) | 78.89 against 77.54 ms | 4/9 |

      - None of the five is outside the noise. Only the group showed (x436b, +5.3%, 8 of 9 blocks).
      - So the frame waits on the sum of several lists, not on one. No single job is the next lever. The lever is SPU throughput across the
        General lists (the runtime's per-job cost), or the main thread's non-poll work. Its polls are not one: the spin hint was neutral
        (x318).
      - EDGE us_run and the walker's us_draw did not move with the lever in any arm.
    - How big those two levers can be (no run; x440's sampler and a static pilot):
      - SPU runtime on the five threads that run the General lists: 17-23% of each thread's samples.
        - Biggest items: `spu_wrch` + `spu_wrch_impl` 4.5-7.4%, `spu_indirect_branch` 2.6-3.9%, `spu_drain_call` 2.1-2.8%,
          `spu_overlay_note_get` up to 2.1%, `mfc_do_transfer` and `spu_coh_is_reserved` ~1.5% each.
        - The thread with 13% in `spu_indirect_branch` runs the wid 8 (audio) job modules, which are off the frame's path.
        - x436b caps the whole lever. Slowing those lists 20% cost +5.3% of frame time, so removing all of the runtime's 17-23%
          would gain about 5%. A realistic cut (1-3% of SPU time) is under 1%.
      - PPU codegen (docs/LIFTER_CODEGEN.md, PPU section):
        - The inline guest-memory range guards are 37-39% of the instructions in the main thread's three hottest functions.
        - A single precomputed span check removes 5-7% of those instructions, ~1% of frame time at most.
        - `ppu_trace_enter` is one load and one branch. Context traffic is 11-14% of the instructions.
      - Both levers are under the in-game A/B's resolution (3-10% between arms), so neither is built. Parked with these numbers.
    - Parked with reasons: walker vertex fetch / FIFO split (no causal share now), the native 0x3FEC0 stub
      (~0.05 core), wid 7 (off the frame's path), dispatch count (lifter architecture: direct call linking).
  - Frame rate: undecided. The paired A/B x309 (runWz = M1.02 against runXc = M1.03 + M1.04, under load,
    warm-up then old new new old, 480 s each) gave 11.59 / 11.79 / 7.42 / 9.15 game frames/s over 360-480 s
    (warm-up, new exe: 13.58). Other programs averaged 1.4 / 1.0 / 1.8 / 2.1 cores (explorer ~0.6-0.8, the
    terminal up to 1.0 in n2 and o2; first read as 2.6-6 cores before the watcher's own WMI share and a
    double-listing were taken out). Spread that wide carries no frame-rate number by the A/B protocol. The profile runs' rates (x301 3.4, x307 4.9, x308 6.6 over 120-480 s) are not
    a claim either.
  - Quiet column (x311, 2026-11-02 21:26-22:08, started after three load samples under 1.3 cores; runWz = M1.02
    against runXd = shipped M1.04, warm-up then old new new old, 480 s, pad masher only): 13.05 / 13.17 / 13.53 /
    13.13 game frames/s over 360-480 s (warm-up 13.47), 10.40 / 12.06 / 12.11 / 11.85 over 120-480 s; waits over
    5 ms 2 / 3 / 0 / 1. The new exe is level or slightly ahead in each pair (~+2% late), inside the noise: no
    frame-rate gain claimed, and no regression. Other programs averaged 1.1-1.4 cores in each arm (explorer
    ~0.65, then CrossDeviceService and the terminal); the WmiPrvSE share `_cpu.txt` showed was the watcher's own
    query (see DEBUGGING.md).
  - x309's new arms had long lock-line waits (`[lockline-long]` 19 and 89, longest 80 and 49 ms; holders PUTLLC
    and `vm_write32`), the old arms none. That was M1.03, not this: in x310 (below) the arms with M1.04 and the
    ack lock had none.
- **M1.03, reverted the same day: SPU event acknowledgements without the lock line (2026-11-02).**
  - Reverted (toolkit b2b3dcca: the ack takes the lock again by default, `SPU_EVACK_LOCK=0` drops it). The
    lock is not needed for correctness, but without it the lock line's tails got worse under an oversubscribed
    host. x310: runXd, same exe, `SPU_EVACK_LOCK=1` against unset, order L N N L, 300 s each with five busy
    processes: waits over 5 ms 0 / 2 / 55 / 0 (longest 18.7 and 24.2 ms, lock-free arms only), game frames/s
    over 120-300 s 5.64 / 4.42 / 6.40 / 7.89. Hard page faults do not explain it (L1 had the most, 1741/s mean).
    Presumably the ack's ~40k acquisitions a second kept the waiters' spin-then-sleep cycling, which gets a
    preempted holder back on a CPU sooner; not proven. A cheaper lock line has to keep that property.
  - Who held the line in N2's 55 long waits: PPU stores 29 (`vm_write32` 13, `vm_write8` 10, `ppu_stdcx64` 6),
    SPU PUTLLC 19, `cellSpursReadyCountStore` 3; one thread was the holder in 36 of them. The same shape as
    M1.02's: a Normal-priority holder kept off the CPU by the game's own above-normal threads. With the ack's
    lock, SPU threads reach the waiters' `Sleep(1)` often, which frees cores for it. The lever this points at is
    the holder's scheduling (thread priorities inside the game), a design item, not the acquisition count.
  - What it measured while it was in:
  - Why: `[lockline-ra]` on the loaded recipe (x306: runXa, two busy processes, 300 s) counted 165k lock-line
    acquisitions a second from 38 registered contexts (steady, none leaked): PUTLLC 31%, PPU `vm_write32` 29%,
    `SPU_WrEventAck` 25%. The ack is the WWS job manager acknowledging its ~40k stall-and-notify interrupts a
    second, and its lock dated from before event updates were atomic (upstream #492).
  - Change (toolkit `spu_channels.c`): the ack clears with the atomic `SPU_EVENT_CLEAR` and re-raises a pending
    Sn without the lock; `list_stall_stat` is only written by the context's own thread.
  - Result (x307 = x301 recipe on runXb, loaded EDGE profile from 120 s): acquisitions 165k/s -> 118k/s;
    `spu_lockline_lock` 3.33% -> 2.09% of EDGE samples, `spu_coh_notify_write` 3.19% -> 2.92%, `spu_wrch_impl`
    1.88% -> 1.50%; the native stages' share rose (`g73_loop1` 5.6% -> 6.7%). 480 s of play, no stall.
  - Frame rate: not claimed. x307's game-frame rate was higher than x301's, but two profile runs on different
    exes are not an A/B. (The WmiPrvSE ~1.6 cores first read from `x307_cpu.txt` was the watcher's own query.)
- **M1.02: no more long stalls on the SPU lock line under load (2026-11-01).**
  - Observed (x295, x296, details under M1.01): under load, single waits on the global SPU lock line of up to
    86 ms, several SPU threads waiting at once, with a normal-priority PPU thread holding the line (a store to an
    SPU-reserved line). Raising the whole process made it worse (x296: up to 425 ms, 42 events in 480 s): the
    holder was being kept off the CPU by the above-normal SPURS kernel threads spinning for it, whose
    `SwitchToThread` yield only reaches threads queued on the same processor.
  - Toolkit a35b95c2: after ~1 ms of spinning (six rounds of 4096 pauses) the waiter `Sleep(1)`s
    (`SPU_LOCKLINE_SLEEP=0` turns it off). Port (`main.cpp`): the game runs its process above normal
    (`GOW_PROC_PRIO=0` turns it off), so the holder also wins against other programs. Either alone was partial
    (x297: sleep only, longest 57 ms); both (x298, x299): 3 and 1 waits over 5 ms in 480 s, longest 12.9 and
    8.1 ms.
  - A/B under load (x300: two busy processes, runWz, warm-up then old new new old, 480 s each; old =
    `GOW_PROC_PRIO=0 SPU_LOCKLINE_SLEEP=0`, process Normal): longest wait 7.4 / 14.0 ms against 34 / 179 ms;
    waits over 5 ms 3 / 38 against 72 / 200+; game frames/s over 360-480 s 8.16 / 8.65 against 8.57 / 2.78. The
    second old run collapsed when the host reached 100%, the starvation this removes; without it the arms are
    level. A quiet column is still owed (not valid while the PC is in use).
  - Harness: the scratchpad `fixgame.ps1` used to force the game's class to Normal, which would undo this; its
    default is AboveNormal now.
  - Quiet column: owed at 01:00 (host at ~22%). Measured for the shipped state at 21:26 (x311, under M1.04):
    M1.02's exe ran 13.05 / 13.13 game frames/s over 360-480 s on a quiet host, against 8.16-8.65 in x300's
    loaded arms. M1.02's own old-against-new quiet A/B (priority and sleep switched off) was not run.
  - EDGE after M1.02, loaded (x301: runWz, two busy processes, `PS3_SAMPLE_WIDS=15 SPU_LOCKLINE_STATS=1`,
    480 s, samples from 120 s; `_cpu.txt` shows the burners, a terminal and explorer, nothing else):
    - Lock waits are gone as a cost: per `[lockline]` line, 33-66 ms waited in total by all threads, over
      65536 contended of 1.1-1.8M acquisitions (~4.5%); longest 0.2-4.5 ms.
    - `spu_lockline_lock` is 3.3% of EDGE samples (6.0% in x292). With waits that small, what is left is the
      atomic exchange on a line every core touches, not blocking.
    - Runtime shares of EDGE samples: dispatch ~11.5% (`spu_indirect_branch_slow` 4.5, `spu_indirect_branch`
      3.4, `spu_overlay_note_get` 2.1, `spu_lookup` + walk 1.5); coherency ~8.5% (`spu_lockline_lock` 3.3,
      `spu_coh_notify_write` 3.2, `spu_coh_is_reserved` 0.8, `ppu_resv_break_line` 0.6,
      `spu_coh_reserve_fast` 0.6); channels ~7.3% (`spu_wrch` 3.5, `spu_wrch_impl` 1.9, `spu_rdch_impl` 1.1,
      `spu_rdch` 0.8); MFC ~3.9%; drain ~2.2%.
    - The native stages lead the lifted code: `g73_loop1` 5.6% (top symbol), `gow_e550_ec08_run` 3.7%. The top
      lifted function is `DCC0` (4.0%), the serial decode EDGE_NATIVE.md defers.
  - Sharding the lock line (audit, read only): not a quick change. The holder list `s_coh_ctxs` is one global
    linked list, walked under the lock by `spu_coh_notify_write(_except)`, `spu_coh_reserve`,
    `spu_coh_unregister` and `spu_coh_forget_range`; per-line locks need that list under its own protection
    first. And if most acquisitions are for the same few SPURS lines, per-line locks would not spread them.
    `[lockline-ra]` (call sites and list length, DEBUGGING.md) is the data for that call.
  - Where three runtime pieces spend it (x301 samples per 16-byte bucket, disassembled from runWz):
    - `spu_coh_notify_write`: 77% of its samples on the load of a listed context's `resv_valid`, the walk of
      `s_coh_ctxs` missing on contexts other threads own.
    - `spu_indirect_branch_slow`: ~2/3 of its samples in the quadword load/store loop of
      `spu_smc_microstep`, which interprets the save and restore stubs the WWS job manager generates at
      interrupt entry (LS 0x3FEC0, 76 `stqd`/`lqd` and a `bi`): ~3% of EDGE.
    - `spu_overlay_note_get` (every GET): mostly the overlay hash-table probe and the content-signature scan
      for GETs of 512 bytes and more.
    - x308 (runXc) inside `spu_indirect_branch_slow` (5.8% of EDGE there): 72% of its samples in the
      `spu_smc_microstep` quadword loop, which is already decode-plus-`pshufb` per instruction; the heaviest
      bucket is the load of the next instruction word. A decoded-stub cache would save the decode only (~1-2% of
      EDGE at best). `spu_lookup` is 1.4% on its own, mostly one probe load.
  - Tried and dropped, 2026-11-02: lifted code's MFC parameter-channel writes (LSA/EAH/EAL/Size/TagID/
    WrTagMask) as inline stores instead of `spu_wrch` calls. Job lab, new capture cap_4 (`work/jobcap4`: 400
    EDGE jobs, 1 in 500 after 150 s, runLabW in call mode; cap_3 no longer replays on any exe since 10-29,
    diverging at the first job's event #17): 400/400 identical both ways; sum of per-job minimums, REPEAT=50,
    five alternated pairs, call 36.5 / inline 36.2 ms median (spread ±10% with the host in use). Under 1%, so
    not kept; the patch is `work/wrch_inline_x302.patch`.
- **M1.01: SPU indirect-branch dispatch takes a frame-less fast path (2026-11-01).**
  - Why: under load the frame is throughput-bound (x277-x279 below), so CPU per frame is the lever. A loaded
    profile of the audio workload (x280: runWz, two busy processes, `SPURS_PM_PROF`, `PS3_SAMPLE_HOST` with
    `PS3_SAMPLE_WIDS=8`) put 40% of its host time in `spu_indirect_branch` (27.5%) and `spu_drain_call` (12.7%).
    `SPU_IBC_COUNT` (x281): ~4.7M dispatches a second, 90.6% cache hits. A hit still paid the full resolver's
    frame (eight saved registers, two XMM saves, a stack cookie) and a call into the interrupt-restore check.
  - A 4096-entry cache (x282, x283) cut refills from 3.5% to 0.7% of dispatches but not the host share; the
    size stays 512.
  - Toolkit b8e3c612: the hit is served by a small `spu_indirect_branch`, everything else by the unchanged
    resolver (`spu_indirect_branch_slow`); `spu_drain_call` returns before building its jmp_buf frame when
    the callee came back with nothing queued. Same recipe, one run each (x284, x285): the dispatch functions
    are ~34% of the audio workload's host time (`spu_indirect_branch` 16.1% + 5.4% slow path; the drain loop
    still runs for 7%, for callees that leave a tail-chain queued). No hang, heartbeats normal.
  - Size: dispatch was ~0.18 of a core in the audio workload; this saves a fraction of that, too small to show
    in a single loaded run's frame rate (the runs also hit the escape at different times: x280 had twice the
    frames of x283-x285, so their per-job costs are not comparable). EDGE spends ~4% in dispatch (M0.95 profile).
  - 600 s smoke run of runWz with no environment set (x291): no hang; out of the opening by 120 s, 10.5 game
    frames/s over 420-600 s on a busy host (82-97% CPU from other programs). An earlier attempt (x290) ran while
    other programs were compiling (host at 100%) and never left the opening; not a regression.
  - EDGE (wid 15) the same way, loaded (x292, `PS3_SAMPLE_WIDS=15`): ~a third of its host time is runtime, not
    job code -- `spu_indirect_branch_slow` 7.9%, `spu_lockline_lock` 6.0%, `spu_coh_notify_write` 4.0%,
    `spu_wrch` 3.9%, `spu_drain_call` 3.4%, `spu_indirect_branch` 2.8%, `spu_overlay_note_get` 2.3%, MFC ~4%.
    The native kernels (`g73_loop1` 6.6%, `gow_e550_ec08_run` 4.6%, `g53_run` 2.1%) and lifted job code are the
    rest. EDGE dispatches (x293, `SPU_IBC_COUNT_WIDS=15,16,17`, ~1M/s): 72.5% cache hits; of the 23.9% that
    miss, ~14 points are SPURS kernel services (module poll / exit), 6.7 refills, 3.6 resident-code-slot
    targets (never cached), 4.2 overlay, 2.5 image.
  - The global SPU lock line under that load (x294, `SPU_LOCKLINE_STATS=1`): ~25k contended acquisitions a
    second, 0.8 us mean wait, 20 ms of waiting per second in all -- but 11 of 135 windows held a wait over 5 ms
    and the longest was 71 ms. With the holder named (x295, toolkit 1cd65827): ~400k acquisitions a second,
    6.5% contended; 13 distinct long-wait events in 480 s, each ~86 ms with several SPU threads waiting at
    once. The holder was a PPU thread in a store to an SPU-reserved line in 24 of the 44 long waits
    (`vm_write32`, `ppu_stdcx64`, `vm_write8`, all one host thread), a kernel thread in `spu_wrch_impl` in 10,
    and a taskset task thread in `spu_taskset_task_exited` in 8. Hypothesis under test: priority inversion --
    a normal-priority PPU thread descheduled by the busy host while it holds the line, with the above-normal
    kernel threads spinning on it. These are hitches (~86 ms), not a frame-rate loss. Splitting the lock by
    line would also cut contention; it guards PUTLLC against PPU stores and the reservation bookkeeping, so
    it needs an audit first.
  - Next candidate: a per-call-site inline cache in the SPU lifter (`bisl` sites check the last target and call
    the lifted function directly) would skip both functions for the common case; it needs the job modules
    relifted and validated.
  - General6 (wid 3, ~0.5 core under load) profiled the same way (x286, `PS3_SAMPLE_WIDS=3`): the native noise
    (`gow_a67e_noise.c`) is ~40% of its samples, the rest the 1bdd job module. Its table lookups as AVX2 gathers
    were exact (x287, `GOW_A67_NOISE=3`: 19.9M calls, 0 differ) but no faster on this CPU (scratchpad
    `a67bench.c`, best of 7: 275 against 258 ns per call); not kept. Port `GOW_A67_COUNT=1` (x289, loaded)
    counts it: ~300k calls a second at ~371 ns each (timer included), 6-7 s of host time per 60 s, i.e. ~0.11
    of a core. A faster noise could save a part of that; the remaining General6 time is the 1bdd module.
- **M1.00: idle WWS lists are probed at least every 4th kernel pass; no more stall clusters (2026-11-01).**
  - Observed (x262-x264, details below): stall pieces of 39-149 ms, with every traced thread waiting and no
    SPU but audio running, that end when `CollisL`, `MeshProcJobs` or `RsxSync` is next dispatched. Probing idle
    modules more often shortens them. Why is only partly known (x270, below): the title does raise readyCount
    for `CollisL` and `MeshProcJobs`, but 1-59 ms after it writes the list header, and their runs follow the
    kick; `RsxSync` runs are mostly found by probing. An earlier version of this entry said the title never
    kicks these lists; that was wrong.
  - The GoW:A port now sets `SPURS_IDLE_MAX=2` (every 4th pass at most; toolkit default 4). A/B on runWw, no
    load, no profilers, a discarded warm-up run first, order i2 i4 i4 i2 (x265), game frames/s over 420-600 s:
    13.44 / 13.46 against 13.19 / 11.32. Over the escape window 12.78 / 11.74 against 12.43 / 10.71. The arrival
    phase gains most: 60-68 game frames/s at 90-120 s against 43-50 in all three default runs (warm-up
    included).
  - `SPURS_IDLE_MAX=0` (every module every pass) removed the clusters entirely in one traced run (x264: longest
    piece 17.5 ms against 149) for ~0.3 more cores; it stays a diagnostic setting.
  - 0 against 2 (x267, runWx, warm-up then i0 i2 i2 i0, 420-600 s): 13.39 / 7.38 against 13.78 / 10.54; escape
    window 12.64 / 11.22 against 12.29 / 11.62. No gain from 0. The two low runs end with other load on the host
    (a browser at 1.1-1.6 cores and the security agent at 0.6-1.2 in i0b; the security agent and the agent
    session's terminal at ~0.9 each in i2b). 2 stays.
  - What load does to the frame (x268: runWx, two busy processes from `burn.py`, `PS3_FTRACE=420:60`, profilers
    off; against the quiet x264). Frame 142.6 ms against 74.0. The EDGE lanes are busy 69-72 ms per frame in
    ~480 runs (61 quiet): each job ~13% slower from sharing physical cores. The time with no non-audio SPU
    running is 10.2 s in 60 (quiet: 0.25 s), the main thread waits 41.9 ms per frame at the task batch (3.0),
    87 of 420 frames hold 92% of that dead time, longest piece 202 ms, and the pieces end with the same chain
    (`CollisL` 289, `MeshProcJobs` 116, `RsxSync` 66 of 590). Under load the game is latency-bound, not
    throughput-bound: the links of that chain are found late.
  - The same load with `SPURS_IDLE_MAX=0` (x269): dead time 4.0 s in 60, longest piece 37 ms, task-batch wait
    31.5 ms per frame, frame 127.8 ms, 468 frames in the window against 420. The probing cost shows: EDGE
    union busy 95 ms per frame (75 in x268). The 30-s heartbeats are about the same (~8 game frames/s in both;
    x269 also had the security agent at ~1.1-1.3 cores). Faster probing only partly helps.
  - What the PPU writes (x270: runWy, toolkit `SPURS_WWS_HDR_LOG=0,7,14,18` with `_AFTER=420`, two busy
    processes; the 400-line cap covered 1.7 s and no trace ran with it). For wids 0 and 14 the header changes
    with the module not having run (the PPU's write) and the kick (readyCount) comes 1-59 ms later; the next
    run with work follows the kick. wid 18's runs mostly come without a kick. In the dead pieces of x268 the main
    thread polls at `0x3436BC` 1.6 times per ms (1.5 outside them; quiet x264 1.3 and 1.5), so it is not slow
    to wake: the work it waits for is not ready yet. "Because the kick is late" was the reading at the time;
    x271-x277 (next bullet) do not support it.
  - Who raises readyCount, and when (x271-x277: runWz, the same two busy processes, toolkit e548526c
    diagnostics). The main thread (guest tid 1, lr `0x46661C`) calls `cellSpursReadyCountStore` for every
    non-audio list, about 80 times a second for `CollisL` and once a frame for `MeshProcJobs` and `RsxSync`; the
    audio thread `brbDriver` (tid 10) kicks wid 8. The SPU modules raise readyCount too: `RenderJobs` (wid 13)
    raises `MeshProcJobs` (x272-x273). The kernel sees a store within 4 ms in more than 99% of cases for wids 0,
    14 and 18 (x275, `SPURS_RC_LAT`), so the recompiler is not where the kick waits. With the stores marked in
    `PS3_FTRACE` (x277, 50 s, 505 dead pieces, 3.1 s): in every piece the list that runs next had already run
    after its last kick. The run that ends a piece is most often a probe that finds nothing (median 31 us);
    the first run that does work after the piece is `MeshProcJobs` in 354 of 505. Still open: why that work
    becomes ready without a new kick, or with nothing scheduling it, under load. Earlier x271-x273 readings
    that "the PPU never raises readyCount" were blind to the HLE store (see DEBUGGING) and are void.
  - The kick itself: the main thread's caller (`func_004649C0`) stores min(pending jobs, the list's maximum)
    every time, with no condition, so the title does not skip kicks. `SPURS_KEEP_READY=1` (the kernel stops
    consuming readyCount) under the same load (x278): 360-450 s 8.1 game frames/s, as x277; not kept.
  - Where the loaded frame goes now (x277 against quiet x263, `ftgaps.py`; x263 is runWw, before M1.00, with
    `SPURS_IDLE_MAX=4`, so the quiet arm is not the same build): 112.4 ms against 79.0. Dead time is down to
    ~7 ms per frame; the main thread's time outside its two largest waits grows ~10 ms, the EDGE union 67 -> 78
    ms, and those waits 12.8 -> 39.5 ms (23.6 at `0x3436BC`, 15.9 at `0x35E7B4` with EDGE busy 92% of it). The
    remaining loaded loss is per-unit slowdown from sharing physical cores with other processes, which host
    priority does not prevent (x257, x258). Using less CPU per frame is the lever -- the same one as the
    frame-rate work.
  - One fewer busy thread does not help: `SPURS_KERN_SPUS=0,1,2,3,5` (no SPU 4 lane, which was busy only
    37.9 ms per frame) under the same load (x279): 3.3 game frames/s at 420-450 s against 8.3 (x277), frame
    235 ms, the main thread waiting at `0x3436BC` 118 ms per frame with no non-audio SPU running 81% of it.
    The `RenJobSet` lists that lane carried queue behind the others. Not kept.
  - 600 s smoke run of runWx with no environment set (x266): no hang; 12.9-14.0 game frames/s from 330 s on
    (its load phase ran slow until 300 s, as load phases vary run to run).
- **What bounds the fast frame now (2026-11-01, x262).** `PS3_FTRACE=480:20` on runWv (M0.99 production) in the
  fast state, no synthetic load, with `SPURS_PM_PROF` and `GOW_MAIN_PROF`; scratchpad `ftgaps.py` reads the
  trace. The M0.60 picture (main thread asleep 310 ms of a 463 ms frame, EDGE one job at a time) no longer
  holds.
  - 191 frames, median 96.7 ms (10.3 fps). SPUs 0-3 are busy 72-76% of the frame, SPU 4 42%, SPU 5 87%
    (mostly the audio workload, wid 8). Work other than audio keeps at least one SPU busy for 95.5 of 104.4 ms
    per frame.
  - The three EDGE workloads (wids 0xF-0x11) now run side by side, as the game registers them (since the M0.85
    lanes, `SPURS_LANES=15,16,17=3`; the M0.60 timeline predates them): each ~65% of a kernel thread, ~5600
    jobs/s and the same job count each (~116 us per job), with no idle polls and 2% in channel waits. Their
    union covers 72.8 ms per frame. wid 3 53 ms per frame (50 jobs per game frame), wid 0xD 29, wids 7 and 0xB
    16 each.
  - Per lane (kernel threads on SPUs 0-3, each running all three EDGE wids): busy 71-73 ms per frame in ~480
    module runs. Gaps under 5 ms between runs total 5-6 ms per frame (median 1-4 us), so dispatch between runs
    is cheap. Each lane also sits idle 25-29 ms per frame in gaps of 5 ms or more, ~31 ms of it in between-pass
    pauses: it found no work. The lanes idle at different times (the union of all non-audio work leaves 8.9 ms
    per frame).
  - Upper bounds: this run had `SPURS_PM_PROF` (a timer read and a lock on every module run), the main-thread
    sampler and the trace on; production runs have neither profiler. The frame rate matched x261.
  - The main thread is blocked 34 ms per frame: 19.4 at `0x3436BC` (the task batch, in `0x368C64`) and 12.6 at
    `0x35E7B4`. The main-thread sampler also shows spin waits that the timeline counts as running: ~12% at
    `0x416A4C` (a RenJobSet job) and 6-8% at `0x467B2C` (WWS job completion). While it waits at `0x35E7B4`,
    EDGE is busy 91% of the time. While it waits at `0x3436BC`, EDGE is busy 39%, and no SPU other than audio
    runs 31% of the time (~6 ms per frame).
  - That dead time is not spread evenly: the median frame has none, and 15 of 191 frames hold 82% of it (pieces
    of 30-63 ms, clustered at 484.4-485.5 s and around 492 s; frame max 322 ms). During those pieces every
    traced PPU thread is blocked (the four fios media threads make no traced calls, so the trace cannot say what
    they do), the RSX walker is idle 86% of the time and only the audio SPU runs. The run that ends a piece is
    `Wws_Job/CollisL` (wid 0), `MeshProcJobs` (0xE) or `RsxSync` (0x12) (38, 30 and 11 of 94 pieces). Cause
    not named: a few stalls, not a wake latency spread over every frame. The run's CPU log has the security
    agent's two processes (`avp`, `kpm`) at 0.4 cores each in the first sample after the cluster, above their
    level elsewhere; suggestive only. `cpuwatch.ps1` now also logs the game's file read/write rates.
  - Not the disk (x263: runWw, which puts the HLE file reads and writes on the timeline; `PS3_FTRACE=420:60`,
    profilers off; 12-13 game frames/s). In 60 s, 536 reads took 20 µs or more, 20 ms in all, the longest
    0.2 ms, all on one thread, and none overlap a stall; no write took 20 µs. The stalls (82 pieces over 2 ms,
    1.35 s in all, ~2% of the time) again come in one cluster, 448.8-450.3 s, pieces of 39-149 ms, ended by
    the same three workloads (`CollisL` 34, `MeshProcJobs` 19, `RsxSync` 17). In both runs the cluster falls
    in a 30-s window where the process working set grows ~30 MB, which suggests the game moving into a new
    area. What the main thread's task batch waits for there is still open.
  - Production settings (x263, profilers off) size it: frame median 71.2 ms (14.0 fps), SPUs 0-4 busy 87-91%,
    main thread blocked 17 ms per frame (7.3 at `0x3436BC`, 5.5 at `0x35E7B4`). The time with no non-audio SPU
    running is ~2% (not the ~9% of x262, which had two profilers on and two clusters in 20 s), and 12 of 758
    frames hold 83% of it. Outside the stall clusters the SPUs are close to full: the frame is SPU compute.
  - The kernel's idle back-off is part of the stall (x264, same recipe as x263 plus `SPURS_IDLE_MAX=0`, the
    diagnostic end: every enabled module every pass). A module that keeps finding nothing is probed only every
    2nd..16th pass (default cap 4 = every 16th, ~27 ms at the measured `Sleep(1)`); see M1.00 for what is and
    is not known about why these lists wait (the title does kick two of them, late). With the cap at
    0: time with nothing running 254 ms in 60 s (x263: 1351), longest piece 17.5 ms (149), main-thread wait at
    `0x3436BC` 3.0 ms per frame (7.3), 810 frames in the 60-s window (758, +7%), game CPU ~7.3 cores (~7.0).
    One run each; a moderate cap is the candidate default, to be A/B'd.
  - Phases (per frame, from the first EDGE run after the flip to the last before the next): the EDGE phase is
    78.6 ms (median) of the 96.7 ms frame, and EDGE runs 92% of it; before and after it 1.9 and 1.6 ms. wid 3
    (`General6`, 50 jobs per game frame) runs 52 ms inside the EDGE phase, on other lanes. The main thread's
    19.5 ms of blocking is all inside the EDGE phase. Each EDGE wid's ~65 ms per frame runs one job after
    another, so the frame tracks the length of those three chains.
  - The RSX walker drains 66 ms per frame and waits on the GPU ~4.
  - Reading: the fast frame is SPU compute, mostly the EDGE jobs; dispatch between module runs is cheap. The
    time with nothing running (~9 ms per frame, ~9%) is mostly a few stall frames. The larger lever is the cost
    per EDGE job, which has no named next step yet: the EDGE profile is 79% lifted code and ~20% runtime,
    register promotion was closed, and the last two native stages gave +3-4% and +2.7-3.8%.
- **M0.99: the frame rate holds better under background load: SPURS kernel threads above normal priority
  (2026-10-31).**
  - Cause: with the host's CPUs busy, the main thread waited ~60 ms per frame for the frame's task batch
    (`0x368C64`) while the SPURS kernel threads napped (x247d1); a kernel thread waking from its `Sleep(1)` pass
    pause at normal priority waits for a CPU at each link of the job chain (details in the M0.98 notes below).
  - Toolkit `SPURS_KERN_PRIO=<n>` (default unset; 0 = normal) sets the kernel threads' `SetThreadPriority` level
    and the `[spurs-kern] ... poll thread live` line prints the level applied. The GoW:A port sets 1.
  - Under a fixed background load (scratchpad `burn.py`: three busy processes at normal priority on all 12
    CPUs), game frames/s over 420-600 s from the heartbeats (the escape-anchored score voids most loaded runs,
    whose steady state is ~8, so the raw window is used): default 6.0, 7.5, 5.5, 4.4 (mean 5.85);
    `SPURS_KERN_PRIO=1` 8.3, 8.0, 8.9, 6.0 (mean 7.8, +33%; the 6.0 run's window still holds its load phase,
    which counts against this arm). Every priority run is at or above every default run (x253, x254).
  - Light load (the agent's terminal at ~1 core, no synthetic load; x255): 8.7 / 7.1 against 4.6 / 7.2.
    A truly idle host is not measured; priority only acts when threads wait for a CPU, so a loss there is
    unlikely but not tested.
  - Caveat, priority inversion: kernel threads spin on a few locks. `ts_lock` (the taskset lock, also taken by
    PPU-side HLE) spun with `Sleep(0)`, which yields only to equal or higher priority; it now falls back to
    `Sleep(1)` after 64 tries. The coherency spin yields with `SwitchToThread` every 4096 tries. On a machine
    with fewer cores than busy threads this is the first thing to check; `SPURS_KERN_PRIO=0` turns it off.
  - 600 s smoke run with no environment set (x256): the kernel threads report `prio=1`, no hang.
  - Lighter fixed load (two busy processes, x259, order k0 k1 k1 k0 k0 k1): `SPURS_KERN_PRIO=1` 7.3, 8.4, 8.7
    (mean 8.1) against 0 at 4.4, 7.8, 8.3 (mean 6.8). The 4.4 run, the batch's first, fell to 1.1-1.6 at the
    end; without it the arms are equal (8.1 against 8.0). At two busy processes the gain is not shown.
  - Batch order check: the first run of a batch is its slowest in four of six batches (x253, x257, x258, x259),
    whatever its arm; in x254 and x255 the first run is the fastest. M0.99 rests on two batches with opposite
    arms first; with every batch's first run dropped it still holds (x253+x254: 8.3, 8.0, 6.0 against 7.5, 5.5,
    4.4). Batches from x260 on start with a discarded warm-up run.
  - Same load, the walker thread above normal as well (port `GOW_WALKER_PRIO=1`, diagnostic, default unset;
    x257, order w1 w0 w0 w1): 4.2 / 8.2 against 7.6 / 8.0. The 4.2 run, the batch's first, collapsed to 1.3-3.2
    at the end; without it there is no gain either way. Not kept. The two default runs repeat M0.99's priority arm (7.8).
  - Same load, the whole game process AboveNormal against Normal (x258, order p n n p): 4.4 / 6.3 against
    4.6 / 6.1. No effect. The whole batch ran slower than x257 on a busier host (CPU samples at 98-100%). A game
    that already wins every priority contest and still loses speed is losing it to the busy processes sharing
    its physical cores (hyperthreading), which no priority setting prevents. Not kept.
- **M0.98: draws that repeat the previous draw's state reuse it (2026-10-31).**
  - Counters (x220/x221, runWl): ~77% of draws have the previous draw's targets, pipeline, textures, samplers,
    viewport, scissor, stencil ref, depth bounds, fragment constants and vertex constants, all triangle lists, with
    no clear, flip, surface read/write or blit-shadow drop in between: only vertex and index data differ.
  - Toolkit 447e7724 (port defaults on): `RSX_DRAW_REPEAT` reuses the render state, targets, surface, viewport and
    depth slot; when `RSX_PIPE_CACHE` also hit and no texture unit samples a current target or the bound zeta,
    the texture handles, samplers and constant buffers too, without binding the constants again. The test is a
    dispatcher state generation with a byte compare as fallback; the per-draw words (NOP markers, vertex array
    offsets, draw and index methods, the per-draw semaphore release) are left out. `VK_CB_REUSE` then points the
    draw at the previous draw's uniform slices. Check mode through the corridor (x222/x224/x227): ~75% of draws
    reuse everything, millions of checks, 0 differ.
  - A/B (x228, busy host at 88-95% CPU, walker CPU per draw): 24.48 / 25.53 on against 24.87 / 26.33 off (-2-3%):
    tex -1.2 us, backend -0.9 us, +0.2 us for the test. Far below the ~30% first estimated: vertex fetch, the
    draw record and the first-use texture hashing stay per draw. A first version that copied and compared 16 KB
    of registers and constants per draw (x225/x226) gained nothing.
  - Bug fixed (toolkit 062d0e5a): `RSX_REMAP_RANGE` (port default since M0.89) took draws whose id range EQUALS
    their reference count. The engine reads "unique == count" as "nothing collapsed" and draws the references in
    order without an index buffer, so a draw naming each id once but out of order (3, 1, 2) came out as 1, 2, 3:
    another winding or other triangles. The range must now be smaller than the count. The engine unit test
    already failed under `RSX_REMAP_RANGE=1`; the test had not been run with the port's knobs on.
    `test_vertex_fetch_plan` now checks the order (7 failures before, 0 after). Toolkit tests should be run with
    the port's defaults as well as the toolkit's.
  - Frame dumps before and after the fix (x228 o2 vs x229, corridor at 360 s and 590 s) show no difference: the
    bug was latent in this scene.
  - Production baseline runWm (x229, all defaults, no stats): 11.47 game frames/s with the host at 87% CPU (another
    process at ~1.2 cores). The quiet-host figure (13.25-13.50 on runWg) still needs a quiet-host rerun of runWm.
  - Where the walker's time outside the exe goes: with the CRT (`VCRUNTIME140.dll`) sampled as the extra module
    (x230) the walker's ntdll+module share is 26.3%, against 24.1% with the NVIDIA ICD sampled (x217). CRT copies
    are therefore a few percent of the walker, consistent with `VK_DRAW_PROF`'s ~0.6 us per draw for the vertex
    and index copies; removing the second pass over vertex data (fetch into the ring slice directly) is not the
    big lever. The walker's remaining split: vertex fetch ~33%, FIFO walk and decoders ~30%, texture hashing
    ~10%, the rest spread thin.
  - Next probes, predictions written before running (2026-10-31). The walker probe (`RSX_SLOW_PCT=25`, -13.6%)
    explains a factor ~0.68 of frame time; the main thread's top samples are waits (SPU job lists, HighestPrio6
    progress, flip), so they say where it waits, not what it waits on.
    - `SPU_SLOW_PCT=25` (new, toolkit `spurs_policy.c`): each SPURS module run that found work is followed by a
      wait of 25% of its own time. Prediction: a fall of 10% or more means lifted SPU code is a co-bound and the
      main-thread waits are its symptom; under 3% means the remainder is GPU, present or sync, and the walker's
      fetch is the only CPU lever left. A run can contain its own waits, so a large fall is an upper bound.
    - Fetch repeats (stats only, `[eng-stats]   fetch repeats in a frame`): fetches whose inputs (layout, vertex
      array offset and format registers, index array, base index, ranges) match an earlier fetch of the same
      frame, and of those, the ones with the same output bytes. Predicted saving of a fetch cache is that share of
      vertices times the fetch's ~28% of walker time (fetch plus remap), less the cost of proving the source
      unchanged. Under ~20% of vertices: drop the idea without an A/B.
    - Result x232 (2026-10-31, same corridor scene in all four runs, author idle): runWg 11.84 / 11.55, production
      (runWn, probe off) 11.15, `SPU_SLOW_PCT=25` 8.99 (-19.4%). That is the probe's ceiling (25/125 = 20%), and
      it is NOT evidence that SPU code is the bound: the wait spins (`SwitchToThread` returns at once when nothing
      else is runnable) on up to six SPU threads of a 12-thread host the game already keeps at 80-100%, so it
      takes cycles from the walker and main thread; and it scales with each run's wall time, waits included.
      Walker (0.68) plus SPU (0.97) shares above 1 say the same. Next: measure job time against module-run time
      (`SPURS_PM_PROF`), and only then a probe that sleeps and scales by job time.
    - The comparisons of 2026-10-31 (x232-x239: production against runWg, the M0.96-M0.98 defaults off, a
      bisect, `GCM_PARK_PAUSE`, `GOW_IDLE_WAIT`) are NOT conclusive. Correlating each run's 360-590 s window with
      the host CPU log: every run from x232 to x237 had 1.4-2.2 cores taken by other `claude.exe` processes (other
      sessions or the claude-mem worker's children) on a 6-core / 12-thread i7-8700K, and the only two runs with
      little of it were the fastest: 14.00 (0.0 cores) and 13.06 (0.8). The others scattered at 9.9-12.3. The
      apparent ~4-5% cost of `GCM_LEGACY_SKIP` (on 11.07 over 10 runs, off 11.54 over 6) matches the arms'
      different contamination (~1.9 against ~1.65 cores) and is not established; no default was changed. The
      quiet-host figure of production is ~13-14 game frames/s, in line with runWg's 13.25 of 10-30.
    - Kept from those runs (a measurement, not a comparison): the SPURS profile (x233p1, `SPURS_PM_PROF=60`).
      The SPU kernel threads are ~4.9 cores busy of the game's ~6.3. EDGE wids 15-17 run ~81% of a thread each
      (~6800 jobs/s each, channel reads 2%, idle polls 0%), wid 8 and wid 3 ~70%. EDGE geometry is the likely
      co-bound; a host profile of those threads comes next.
    - Correction (x240-x243): most of the foreign load was the author's own agent process and the terminal that
      draws it (0.4-2.9 cores while merely waiting), plus other jobs on the host (an ffmpeg batch of ~4.5 cores,
      Disk Cleanup, the NVIDIA container). "Wait for a quiet host" does not work here: explorer alone takes ~0.3
      cores, and the gate often gives up.
    - Pinning the game to CPUs 0-9 (`0x3FF`, the agent and terminal on 10-11) COLLAPSES it: 2.44 and 8.95 game
      frames/s in two runs of the same scene, the game using only ~2.5-4.7 cores (x242) with 10 CPUs free to it.
      The mechanism is not established: the game was mostly idle, not CPU-starved, so some thread sleeps or waits
      where it should be woken (a wait or scheduling pathology, which may cost a little at 12 CPUs too). Never
      restrict the game's affinity in a benchmark.
    - With the game on all 12 CPUs and the agent and terminal on 10-11 at below-normal priority (x243), identical
      production runs still gave 11.00 and 8.69. A single run's frames/s carries ~+-15% host noise on this
      machine, so a 3-5% A/B is out of reach with a few runs; use the per-job medians (`pmprof_edge.py`,
      `frame_wait.py`) or effects of 15% and more. `GOW_IDLE_WAIT=1` gave 11.59 in its one valid run: no
      conclusion, default unchanged.
    - `GCM_LEGACY_SKIP`: closed. No batch showed a consistent difference; the default stays on.
    - Fetch repeats (x242s1, `ENG_PHASE_STATS=60`): per minute ~1.06M fetches and ~332M vertices, of which only
      645 fetches (5160 vertices, 0.002%) repeat their inputs within a frame. A fetch cache is dropped without an
      A/B.
    - The EDGE host profile wanted above already exists (M0.95 era, `PS3_SAMPLE_WIDS=15,16,17`): lifted code 79%,
      runtime ~20% in pieces of 1-4% each, and register promotion in the lifter as the remaining double-digit lever.
    - What binds now (x246s2, game on all 12 CPUs, last 60 s block of `ENG_PHASE_STATS=60` in the corridor): the
      walker is busy 43.5 s of 60 s, its draw ends take 40.2 s, vertex fetch 17.0 s, backend 9.3 s, textures
      7.7 s. The walker is the main bound. Its largest piece is the fetch.
    - GPU reads of guest memory (`VK_EXT_external_memory_host`): NO-GO. The 3080 has the extension (import
      alignment 4 KB), and guest main memory is one committed, page-aligned region, so the mechanics would work.
      The gate was the data's lifetime. Toolkit `RSX_VTX_LIFE=<n>` (new, default off) hashes, for every n-th fetched
      draw, each attribute's referenced source range and each index run. It hashes them again when that frame ends
      and when the next one ends. x244 (n = 4, in game, 360-480 s): of ~714k vertex ranges a minute, 473k (82% of
      the bytes) changed before their own frame ended, and 42k more before the next frame ended. Of ~929k index
      runs, 599k changed by the frame end and 152k more by the next. The game reuses its vertex and index rings
      within the frame, right after the RSX has passed them. A GPU that reads guest memory a frame later would see
      other data, so the copy the walker makes at the draw is required, and the GPU-side gather is not a lever.
    - Upstream PRs #629-#635 (another GoW:A port on the same toolkit, MIT) reviewed against our tree. Nothing there
      speeds up our build: we already have the SPU dispatch, overlay relocation, float and shuffle work in
      equivalent or better form, and #630 removes a hand-off our design does not have. Taken: #629's fix for
      `spu_taskset_parked_add`, where two tasks parking at once could take the same slot (claim with a
      compare-and-swap, then fill; toolkit c7f81d26, 600 s smoke run x245 without a hang). That is a correctness
      fix. Deferred (small, off the frame's critical path):
      SSE4.1 quadword shifts for job modules, per-NID and per-thread lookup caches on the PPU, and the
      local-offset flag written only on change.
    - Slow stretches (x246-x252, 2026-10-31 afternoon). Runs drop for minutes to 2-7 game frames/s and recover;
      this, not small host noise, is most of the run-to-run spread. What is known:
      - From ~120 s to 150-360 s every run is slow: the level loads and Kratos arrives. Where that phase ends
        varies by run, and a fixed 360-590 s window sometimes lands in it. New scoring (scratchpad `escape.py`):
        escape = after 150 s, the first three consecutive 30-s heartbeats at 8 game frames/s or more; window =
        escape+60 .. escape+240 s; no escape = void. Re-scored x241-x252 production: 9.0-12.5, typically ~10.5-11.5.
      - Some runs stay at 4-7 in the corridor scene itself (x251p1-p3 all run, x249t2 after 240 s), with Kratos at
        the same spot as in fast runs (frame dumps), so it is not a scene of its own. In most of them the Windows
        Terminal drawing the author's agent session took 0.7-2.7 cores and the host was at 90-100%; x249t2 had no
        terminal load. Cause open.
      - Ruled out with data: the timer (heartbeat `sleep1`/`tres`, new in the port: `tres` stayed 1.00 ms and
        `Sleep(1)` 1.1-4 ms in slow and fast windows alike); the window present (it never runs with the window
        hidden); interrupts, DPCs and hard page faults (no rise in slow windows). Game priority AboveNormal
        against Normal (x252) was not tested under load: no terminal load came up in that batch.
      - The `0x3FF` collapse (x242) reads as the same thing: the game short of CPUs.
      - In a slow window (x247d1, `GOW_MAIN_PROF`) the main thread spends 28-33% of its samples waiting at
        `0x368C64` for the frame's task batch, against ~10% in a fast one (about 60 against 9 ms per frame),
        while the SPURS kernel threads mostly sit in ntdll. The kernel threads nap with `Sleep(1)` between passes;
        on a full host a thread waking at normal priority can wait a quantum for a CPU, once per link of the
        frame's job chain.
      - Toolkit `SPURS_KERN_PRIO=<n>` (new, default unset): `SetThreadPriority` level for the kernel threads.
        Under a fixed synthetic load (scratchpad `burn.py`, three busy processes at normal priority on all CPUs;
        x253, order b0 b1 b1 b0, game frames/s over 420-600 s from the heartbeats): default 6.0 / 7.5,
        `SPURS_KERN_PRIO=1` 8.3 / 8.0. The load alone takes production from ~11 to ~7.5. One default run fell to
        2.9 at the end, and neither priority run did. Suggestive, not established: two runs per arm.
      - Per-thread CPU in a slow run (x260d, runWv, no synthetic load, 2-5 game frames/s from 210 s on; scratchpad
        `thrsnap3.ps1` reads each thread's start address and maps it to the `.map`; 60-s windows over 300-540 s):
        the game uses 2.8-3.5 cores. RSX walker (`frame_clock`) 0.60-0.72 cores (0.19-0.27 of it in the kernel);
        one SPURS kernel thread 0.70-0.75; the other five 0.11-0.39 each; the PPU main thread only 0.21-0.31.
        No thread spins near a full core, so contention with the runtime's own spinning is ruled out as the cause
        of slow stretches. The host was not quiet: other processes kept it at 75-100% busy (file explorer up to
        1.2 cores; ~0.9 cores of python from an unrelated job). The earlier run x260c, on the same host, held
        6.4-9.2 over 300-600 s. (`x260d_thr.txt` window labels are 243 s behind game time: the snapshot script
        was restarted mid-run.)
      - Hidden host load. The run CPU logs (scratchpad `cpuwatch.ps1`) listed processes from `Get-Process`, which
        reports no CPU for processes it cannot open (services, the security agents on this host), so "no other
        load" never meant a quiet host. The total busy figure counts them: across 25 runs without synthetic load
        (x240-x260, 240-600 s, 244 samples of 30 s), in windows under 6 game frames/s the rest of the host used
        5.7 cores (median; 3.5 of them invisible to `Get-Process`) against 3.2 (1.8 invisible) in windows at 8 or
        more. Slow windows coincide with ~2.5 more cores of other work. The busy figure is an instantaneous
        sample, so this is a correlation over many samples, not a per-window measurement; what the invisible load
        is was not recorded. It holds after the load phase too (22 runs that escape, samples after the escape:
        invisible 3.9 cores in the 10 windows under 6 against 1.9 in 175 windows at 8 or more), so it is not only
        the security agents scanning the level load's file I/O. Its direction is not established: the correlation
        with the game's rate is the same with the load 30 s before, at the same time and 30 s after (-0.35 to
        -0.38), and idle-priority background work grows whenever the game uses less CPU. The burners establish
        only that load slows the game. `cpuwatch.ps1` now also lists the top processes by performance counter (`perf=`),
        which sees every process. The same build reads differently by time of day: daytime production runs on
        2026-10-31 scored ~10.5-11.5, while the default arms between 00:20 and 05:05 the next night, on a
        quieter host, read 12-14 (x263 trace median 14.0 fps; x265 default runs 13.19 / 11.32). Compare A/B arms
        only within one batch.
      - Fast-state census (x261, same build and recipe, 10-11 game frames/s from 330 s on, 60-s windows over
        300-600 s): the game uses 4.9-5.5 cores. Walker 0.69-0.76; the same single hot SPURS kernel thread
        0.74-0.76; the other five 0.35-0.73; PPU main 0.55-0.64. Against the slow run (x260d), the hot kernel
        thread and the walker barely change; the PPU main thread and the other five kernel threads do half the
        work or less. The hot thread costs the same ~0.75 cores at 3 and at 10 frames/s, and has 3-5 times the
        kernel time of its peers: its work does not scale with frames. It is the SPU 5 thread
        (`SPURS_KERN_SPUS=0,1,5`), which runs wid 8, `Wws_Job/BrBDblBufWrkld`, the game's real-time audio DSP
        (M0.60). `SPURS_PM_PROF` in x233p1 and x247d1: wid 8 runs 2500-3500 jobs/s and 67-77% of a thread at
        every frame rate (263-511 jobs per game frame), while wid 3 runs exactly 50 jobs per game frame. The audio
        is paced in real time; `PS3_MUTE=1` only silences the device. Not on the frame's path; its ~0.75 cores
        only matter as one more busy CPU.
      - Other load seen by the new `perf=` field: the WMI provider host at ~0.4 cores, with ~2-core bursts roughly
        every minute; explorer bursts of 1.4-3.4 cores together with the NVIDIA display container (~0.8) and
        `nvcontainer` (0.4-0.9) in the same samples, about every 110 s (t = 175, 286, 396, 646; no explorer window
        was open, and the bursts do not line up with the frame dumps): about one burner's worth, periodically; the security agent `avp` at 0.1-0.4;
        the author's agent session (Windows Terminal, claude) at 0.4-2.
      - The monitor itself is not the hidden load. The WMI provider host sat at 0.66 cores over 30 s with no
        `cpuwatch.ps1` running and at 0.53-0.68 with it. The per-process counter query (`sys=`) arrived in
        `cpuwatch.ps1` at x250u1; the slow/fast split of the invisible load is the same before (3.4 / 2.2 cores,
        11 runs) and after (3.5 / 1.6, 17 runs).
    - Kept, default off: `GOW_IDLE_WAIT` (port: 0 = `Sleep(4)` after a walk that caught up with put, 1 = the
      toolkit's kick wait, 2 = `Sleep(1)`) and the fetch-repeat stats. `SPU_SLOW_PCT` and `GCM_PARK_PAUSE` were
      removed (void and neutral). Benchmarks run muted (`PS3_MUTE=1`).
- **M0.97: the vtable decoder only sees its side-effect methods (2026-10-30).**
  - Host profile of the walker at 1 ms (x217, runWh, per-method clocks gone, exe code only): vertex fetch ~33%
    (`rsx_vertex_fetch_many` 19.7%, `rsx_vertex_remap_build` 8.5%), FIFO walk and decoders ~32%
    (`gcm_rsx_process_fifo_unlocked` 14.5%, `rsx_process_method` 9.5%, `rsx_dispatch_method` 4.4%,
    `rsx_draw_engine_method` 3.3%), texture hashing ~12%, `sink_end_draw` 7%.
  - With the engine drawing, no vtable backend is registered, so `rsx_process_method`'s `rsx_state` feeds only
    backend calls that never happen. Its effects outside that state are the reference (0x50), notifies (0x104,
    0x180) and semaphores (0x1A4, 0x1D6C-0x1D74). Toolkit `GCM_LEGACY_SKIP` (ef4a4df7, port default on) sends
    it only those.
  - A/B (x219, runWj, busy host: another process at ~1-1.5 cores): walker CPU per draw 26.23 / 25.25 on against
    26.44 off (o1, 27.36, is contaminated: tex 13 us per draw). The -2.6% equals the run-to-run spread; the change
    stands on its exactness and on removing the decoder's ~9.5% of the walker's exe samples by construction. The
    production build with M0.96 + M0.97 still needs a quiet-host baseline.
  - `RSX_PIPE_CACHE=2` through the corridor (x216, 600 s): ~10M hits, 0 differ.
  - Tried, not kept: `s_fifo_getoff` on its own cache line (it shares one with the pump's locked flags): walker
    CPU per draw 26.70 / 25.50 against 25.43 / 25.75 (x218). Guest memory under `MEM_WRITE_WATCH` to replace
    texture hashing: a microbenchmark puts the first write to each page after a reset at ~1 us and a 64 MB
    stream at 3x the plain time, more than the hashing costs.
- **M0.96: the pipeline lookup reuses the previous draw's answer; exact, frame-rate gain below noise (2026-10-30).**
  - Counters first (x206/x207, runWe, per 30 s, ~920k draws): `eng_pipeline_get` 1.9 us per draw, ~85% of the
    pipe phase. The key repeats the previous draw's on 84% of draws (0x08E4 83%, texture registers 83%, 0x1EA0 88%).
    The fp memo hits its last slot 83%, another slot 6%, misses 10%; only a quarter of the misses are the same
    structure with other constants, and fill plus reseed cost 0.32 s of 1.83 s. The title uses well over 12000
    fragment-program addresses per 30 s, so the address- and structure-keyed memo of the design note would buy
    little; dispatcher write generations are not needed either.
  - Toolkit `RSX_PIPE_CACHE` (port default on): a signature of every input the lookup reads (program registers,
    the texture unit words the masks come from, vertex program bytes, fp memo slot and fill serial, layout,
    targets, render state less ALPHA_REF) returns the previous answer; the fp memo still compares the program
    bytes in full. Check mode (x208, 300 s): 3.3M hits, 0 differ; ~78% of draws hit.
  - A/B, one exe (x209 g o o g + x210 o g, quiet host, pre-ce506acf harness with per-method clocks): pipe
    2.25 -> 1.50 us per draw, walker -0.4..0.6 us per
    draw; on 13.50 / 13.25, off 13.68 / 13.19 / 13.18 game frames/s (x209 g1 void: methods +10%). At the 0.78
    causal factor a 2% shorter walker predicts ~+1.6%, below this A/B's noise; the walker's share is re-measured
    next.
  - Follow-up (same day): the causal probe on runWg (x211, `RSX_SLOW_PCT=25`) gives 11.45 against 13.25 game
    frames/s (-13.6%), so the walker is still the main bound (factor ~0.68). A host profile of the walker thread
    (x212, `PS3_SAMPLE_HOST`, exe code only; driver time is not visible) puts ~31% of its samples in the FIFO
    walk and the two method decoders (`gcm_rsx_process_fifo_unlocked` 14%, `rsx_process_method` 7.7%,
    `rsx_draw_engine_method` 5.9%, `rsx_dispatch_method` 3.4%), ~33% in vertex fetch (`rsx_vertex_fetch_many`
    20%, `rsx_vertex_remap_build` 7.9%, element decode 4.4%) and ~10% in texture hashing.
  - Part of that FIFO share was the harness: `ENG_PHASE_STATS` timed every method with two clock reads. Toolkit
    ce506acf counts methods only and prints the walker's CPU time per draw instead (`cpu:` line).
  - Tried, not kept: a packet fast path in the FIFO walk (`GCM_FIFO_FAST`: one IO translation per packet, the
    debug knobs tested once, same calls in the same order). Walker CPU per draw (x215, busy host) 25.48 / 25.59
    on against 26.09 / 25.33 off: no measurable change.
  - Same counters, texture hashing: 1.42 s per 30 s on the walker, sparse checks 0.73 s (~245 textures a frame,
    ~7 us each, cache misses on large textures) and full hashes 0.69 s (~7 GB per 30 s).
- **M0.95: the fp constant block is not cleared per draw, +6% (2026-10-30).**
  - `rsx_fp_collect_constants` and `eng_pipeline_get` each cleared the whole 32 KB inline-constant block on every
    draw; every reader stops at its count and every slot below it is written whole. Toolkit `RSX_FP_NOCLEAR`
    resets only the count; port default on.
  - A/B, one exe (x205, g o o g, quiet host): pipeline lookup 3.5 -> 2.0 us per draw, walker 24.2 -> 22.2 us per
    draw; 13.52 / 12.83 against 12.14 / 12.74 game frames/s.
  - Next: the pipeline cache with register write generations (`work/dirty_groups_design.md`); first count the
    fp-memo hit rate, distinct fragment-program addresses per frame and how often the texture and vertex-program
    groups stay unchanged between draws.
- **M0.94: the main thread's flip polls sleep, +6-11% on a busy host (2026-10-30).**
  - After M0.89-M0.93 the CPU is the limit: x171 and x188 stood at the same spot, and the SPU kernel threads'
    busy time per frame grew 64 -> 79 ms for the same workload shares, i.e. they share cores with a busier main
    thread and walker. The main thread's 30 us flip polls (`0x375A88`, ~17% of its samples) spun on the
    runtime's yield loop because `PS3_USLEEP_WAIT_US=500` only puts sleeps of 500 us or more on the timer.
  - Port default now `PS3_USLEEP_WAIT_US=20`: those polls wait on the high-resolution timer (~0.5 ms floor).
    A/B (x193, runVv, a terminal at ~0.9 cores through all runs): same-spot pair 11.53 against 10.35 game
    frames/s (+11%); the first on run 10.99 at the same spot (the first off run stood elsewhere, void).
  - The vertex fetch phase split (x195, runVx, per 30 s, ~26k draws/s): index decode 0.80 s, dedup/range
    1.11 s, fetch plan 0.43 s, `rsx_vertex_fetch_many` 3.15 s of 6.0 s; the index-buffer build is another
    0.76 s. Decoding on the GPU removes the 3.15 s; uploading the raw (byte-swapped) indices with the draw's
    lowest id as Vulkan's vertexOffset would also remove most of the range and index-buffer work (~5.8 of the
    walker's 21.4 s of methods).
  - Tried, not kept: an SSE4.1 `rsx_vertex_fetch_many` (bit-exact against `rsx_vertex_fetch_one` over the new
    8000-plan test, 3x faster than the scalar loop under gcc, 25% under clang) left the in-game fetch time at
    ~4.0 us per draw (x196, exe A/B, clang-cl already vectorizes the scalar loop). The strengthened test stays.
  - Tried, kept off: toolkit `RSX_VTX_GPU` (53d1d2aa): draws whose references are consecutive elements inside
    each attribute's stable range decode their vertices on the GPU (raw guest bytes in a ring, a compute pass in
    a prologue command buffer ahead of each batch). Exact (check mode: ~270k draws, 0 differ), but only ~55% of
    draws qualify and the declined ones (sparse index ranges, other layouts) carry most of the fetch time: CPU
    fetch 3.4 -> 2.6 s per 30 s while the raw copies raise the backend 5 -> 7 us per draw (x199/x200, busy host).
    No frame-rate gain. A gather path for deduplicated references would be needed before it pays.
  - Tried, not kept: the main thread's job-slot scan (`0x416338`, called each round of its job-list polls)
    sleeping ~0.5 ms every 64th call (a PPU trace-hook knob, both arms with the hook live; x201, s z z s):
    11.44 / 11.74 against 11.94 / 11.00 game frames/s, thread CPU unchanged. Neutral.
  - Vertex fetch conclusion: two exact rewrites (SIMD, GPU decode) left the CPU cost in place; it is the
    walker's first touch of vertex data the SPU threads just wrote (cross-core misses), not the decode.
    Next on the walker: raw indices with `vertexOffset = -lowest id` (index decode + range + index build,
    ~2.7 s of every 30 s), then dirty register groups for the pipeline lookup.
  - Tried, not kept: index buffer and references straight from the raw index run (triangle list, one run, no
    restart: `id - lowest`, refs lowest..highest; check mode 88k draws, 0 differ). Only ~7% of draws qualify
    (~1750 of ~26k a second): 10.31 / 10.19 against 10.15 / 11.69 (x204). Most draws are strips, several runs
    or arrays.
  - Production reference is unchanged by M0.94's experiments (runVz/runWa with the knobs off = runVw).
  - Ceiling on the walker path: with the causal share (~0.78) a vertex fetch rewrite (~5 us of ~24 per draw)
    gives ~+16%, ~14 game frames/s at a quiet host; beyond that the main thread's job-list polls bind.
- **M0.93: draw recording without repeated state, descriptor sets reused (2026-10-30).**
  - Where the walker's backend time goes now (x190, `VK_DRAW_PROF`, ~2500 draws a frame, 96% into an open render
    pass): descriptor set 1.5 us per draw (allocation 0.3, uniform copies 0.3, view/sampler resolve 0.2, update
    0.5), vertex/index copies 0.6, command recording 1.9, batch flush 1.0-1.1.
  - Toolkit `VK_STATE_SKIP` (port default on): the pipeline, viewport, scissor, stencil reference and depth
    bounds are set only when they differ from what the command buffer already holds. A/B, one exe (x191, k o o k):
    recording 1.9-2.0 -> 1.4-1.5 us per draw; 12.31 / 12.45 against 12.35 / 12.29 game frames/s (within noise).
    Exact, so it stays on.
  - Toolkit `VK_UB_DYNAMIC` (port default on): the uniform bindings take dynamic offsets, so a draw with the
    previous draw's views and samplers binds its descriptor set again. A/B, one exe (x192, u o o u, same spot):
    descriptor work 1.5 -> 0.9 us per draw, backend 5.0 -> 4.5; 12.53 / 12.29 against 12.19 / 12.41 game
    frames/s (within noise). Frame dumps look the same.
- **M0.92: the MLAA input copies stay on the GPU (2026-10-30).**
  - With the new defaults the walker's largest remaining GPU wait was the readback before NV3089 blits (x182
    trace: ~1.05 s per 20 s). Logged blits (x183, `NV3089_GATE`, 5 s): every one is a 1:1 copy of the display
    buffer (three, in local memory) into main memory at offset `0xAB0000`, pitch 5120, in two strips (1024 and
    256 columns), ~2 a frame: the input of the SPU MLAA. Its taskset is served by HLE (M0.17), which never reads
    those pixels, and the frame then samples that memory as one 1280x720 linear A8R8G8B8 texture.
  - Toolkit `RSX_BLIT_SHADOW` (d1571122): such a blit is copied on the GPU into a strip texture keyed by its
    destination (backend op `image_copy`, batched); a texture fully covered by strips gets them assembled on the
    GPU. Guest memory is not written. Port default on only while MLAA is HLE (`GOW_MLAA=lifted` leaves it off).
  - Check mode (`=2`, x185): ~4750 assembled textures, 0 texels differ from the guest-memory copy.
  - A/B, one exe (x186, 360-590 s, quiet host): texture re-uploads ~630 -> ~300 per 30 s; 12.32 on against
    11.95 / 11.94 off in the same spot (+3%); the second on run stood elsewhere (12.80).
  - Tried, not adopted: `VK_DRAW_BATCH_MAX=512` (x187, one exe, m d d m): backend recording 3.9-4.1 against
    5.0-5.6 us per draw, but the texture phase rose 4.6-4.9 -> 7.0-7.8; 12.05 / 11.86 against 12.40 / 12.02.
    The default (128 with `VK_ASYNC`) stays.
  - The bound after M0.92 (x188, runVr, timeline 420-440 s under PS3_FTRACE, so 11.5 rather than ~12.4 at speed;
    thread CPU 400-425 s): the main thread waits ~4 ms a frame for the flip (21-31 ms before M0.89), the walker
    drains 79 of an 87 ms frame with ~2 ms of GPU waits, five SPU kernel threads are ~90% busy. Thread CPU:
    main 0.92, walker 0.84, SPU threads 0.81-0.83, sum ~6.3 cores on the 6-core host.
  - Causal probe (x189, runVr, quiet host): `RSX_SLOW_PCT=25` (the walker busy-waits 25% of each pass) gives
    10.55 against 12.48 game frames/s, -15.5% where a fully binding walker would give -20% (M0.80: -13%). The
    walker is still the main bound; 12.48 is the production rate at the door at speed.
  - The old "SPU MLAA off: no gain" (M0.6x) was measured at ~2.3 fps with a patch that disables the SPU tasks,
    not these GCM copies; it says nothing about this path.
- **M0.91: colour snapshots join the draw batch, +4-7% in the corridor (2026-10-30).**
  - A draw that samples a target it is drawing into gets a copy of it (`color_snapshot`, ~11 a second in the lit
    passes). Each copy was submitted on its own with a wait for the GPU (~2.7 ms). Toolkit `VK_SNAP_BATCH`
    (5d905815) records it into the open batch after a full barrier; port default on.
  - A/B, one exe (x180/x181, 360-590 s): walker snapshot time ~0.95 s -> ~6 ms per 30 s; walker 24.9 against
    27.1 us per draw and 11.33 against 10.61 game frames/s in the same-scene pair (+6.8%, a terminal at ~0.9
    cores in both runs); 9.86 against 9.48 in the second pair (+4%, the two runs stood in different places).
    Frame dumps look the same.
- **M0.90: colour targets read in another format converted on the GPU (2026-10-29).**
  - Two half-resolution A8R8G8B8 targets (640x360 at `0x024A0000`, 320x180 at `0x029F0000`) are sampled as linear
    Y16_X16_FLOAT (~2-3 reads a frame). Each read went through guest memory: a readback that waits for the GPU
    (~2.9 ms), a CPU convert and a re-upload. Toolkit `RSX_C2RG16` (bc1d7ea9) regroups the bytes on the GPU (image
    -> buffer -> compute pass -> R16G16F texture) inside the open batch; each target keeps two converted shapes.
  - Check mode (`RSX_C2RG16=2`, x177/x178): ~20000 conversions, 0 texels differ from the guest-memory path.
  - A/B, one exe (x179, 360-590 s): the walker's store time 1.45 s -> 0.02 s per 30 s, texture phase 7.2 against
    8.5 us per draw; 9.66 / 9.46 on against 9.51 off (the fourth run void). A terminal used ~0.9 cores through all
    four runs, so the absolute rates are low. Port default on (`RSX_C2RG16=1`, main.cpp).
  - RPCS3's RSX code, read for ideas only (GPL-2.0; nothing copied): per-draw work is skipped unless the
    registers it depends on changed (dirty groups), textures are cached per unit with guest page write
    protection instead of hashing, vertices go to the GPU raw and the vertex shader decodes them, descriptor
    sets and constants are reused. Those four are the next walker steps, in that order.
- **M0.89: the RSX walker binds the frame again; range fetch and render-pass merging on by default (2026-10-29).**
  - Per-thread host CPU at speed (x168, `Get-Process` thread times over 400-430 s, quiet host): main thread 0.97
    cores (about half of its sampled guest time in the job-list polls `416338`, `466E38`, `416610`), RSX walker
    0.89, SPU kernel threads ~0.6 each. Frame timeline (x166, 420-450 s, one other program at ~0.7 cores): the walker
    drains the FIFO for the whole frame (117 ms per 111 ms frame, 11.8 ms of it waiting on synchronous GPU
    submits), and the main thread waits 29 ms per frame at `0x375A88` for the flip counter.
  - The kernel threads nap ~33% of the time between empty passes (`Sleep(1)`, median 1.7 ms). Toolkit
    `SPURS_PASS_SLEEP_US` (abfaa4bc) waits on a high-resolution timer: with 200 the nap is ~0.53 ms but the threads
    still nap ~28% (they have no work). A/B, one exe (x170, 360-590 s): 11.55 / 11.46 against 11.44 (the fourth
    run had a terminal using ~0.7 cores): no change. Default off. `SPURS_PASS_SLEEP=0` (x169) makes every kernel
    thread spin at ~0.94 cores; that run had ~1.6 cores of other load and is not counted.
  - Walker profile (x168, 50 ms sampler): `rsx_vertex_remap_build` 19.6%, `rsx_vertex_fetch_many` 12%, FIFO decode
    8%, texture content hash 5.5%. `ENG_PHASE_STATS`: ~2400 draws per frame, ~28 us each (backend recording ~8.5,
    vertex fetch ~7.5, textures ~7, pipeline lookup ~3.5).
  - Toolkit `RSX_REMAP_RANGE` (4e04a0a9, default off): a dense indexed draw takes its id range instead of
    deduplicating references. A/B, one exe (x172/x173, 360-590 s): clean pair 11.97 on against 11.21 / 10.91 off,
    vertex fetch 6.0 against 7.0-7.2 us per draw. The other three runs had a terminal using ~0.85 cores (and two
    of them stood elsewhere in the scene): 9.40 on, 9.19 off / 7.85 on. Not adopted until a clean pair confirms
    it. Its frame dumps look the same as the off runs (visual check, not pixel-compared).
  - Second pair (x174): 10.07 on against 10.31 off, but the on run's walker cost 38.4 against 31.1 us per draw
    (texture phase doubled: another part of the scene) with a browser at ~1 core; not comparable. Vertex fetch
    fell in every pair (7.0-7.6 -> 6.0-7.1 us per draw).
  - Toolkit `VK_PASS_MERGE=1` (M0.81, off until now) on top, A/B one exe (x175, range on in both arms):
    backend recording 4.8 against 8.4-8.6 us per draw, the walker's whole method time 26.9 against 29.1-29.4 us
    per draw (-8%). Same-scene pair (both at the door): 11.18 on against 11.04 off. The other two runs stood by
    the statue; the merge one is void (terminal ~0.9 cores, texture phase doubled).
  - Both are port defaults now (`VK_PASS_MERGE=1`, `RSX_REMAP_RANGE=1`, main.cpp; 0 goes back): they take ~10%
    of the walker's time per draw and the frame dumps look the same, but the frame rate moves only +1-3%. The
    walker and the main thread bind together: in x171 the main thread is busy ~55 ms of an 84 ms frame, so a
    free walker would cap near ~16 game frames/s before the main thread's job-list polls bind.
  - 20-minute soak with the new defaults (x176, environment unset): no hang, steady memory (2.2 GB private),
    frame dumps right from the menus to the door. Its rate (7.8) is not counted: a terminal used 0.9-1.8 cores
    for the whole run.
  - Synchronous GPU waits on the walker (x171 trace, 20 s): `vk_eng_color_snapshot` 0.61 s, `vk_eng_readback`
    0.54 s, `vk_eng_texture_create` 0.40 s, ~7.5% of its time. Next walker targets with the texture phase.
  - Measuring: winsheet first; `methods` us per draw (ENG_PHASE_STATS) more than 10% apart voids a pair.
- **M0.88: General6's Perlin noise as native code: the job is 47% faster, the frame rate does not move (2026-10-29).**
  - The hot block of job module a67e (`4930`, falling into `4AB0`'s entry block) is Ken Perlin's improved noise in
    3D, four points per call (point i in words 0-2 of r(3+i), result in word i of r3). The callers sum four octaves.
    The tables are read from local store at run time (permutation at `0x8680`, gradients at `0x8580`).
    `port/gow_a67e_noise.c` computes the four points side by side with the same `spu_helpers.h` float ops in the
    same order, so every lane is bit-exact. `port/gen_a67e_noise_lift.py` puts a switch in front of the lifted
    function (`GOW_A67_NOISE`: unset/1 native, 0 lifted, 3 check).
  - Job lab (300 captured a67e jobs): native 300/300 identical in final registers, LS and outputs, median 0.92 ms
    per job vs 1.74 ms lifted (-47%). Check mode in the lab: 300/300 jobs, no difference printed.
  - In game (x165, `SPURS_PM_PROF`): General6's kernel-thread time fell from 77% to 52% of the window for the same
    job count, and the host sampler on it now shows the noise at ~23% of its time (was ~45%).
  - Corridor A/B (x164, 600 s runs, 360-590 s, order native/old/old/native): 10.36 vs 10.35 game frames/s in the
    first pair. The second pair (8.98 old, 6.29 native) is not counted: other programs used one to two cores
    through both of its runs (video encoding, an archive, a GPU stress test, a browser). The frame rate is
    neutral: General6's compute is no longer what the frame waits for. The native path stays the default since it
    is exact and frees a quarter of a host core.
  - The main thread's sampled waits (`GOW_MAIN_PROF`) are unchanged by it: ~20% of its time at `0x375BF0`
    (lr `0x375A88`), ~13% at `0x467B2C`, ~12% at `0x416A4C`, in both the old (x160) and the new (x165) build.
- **M0.87: SPU floats with flush-to-zero, quadword local-store access in one load, +4% in the corridor (2026-10-28).**
  - Toolkit `SPU_FP_DAZ` (on whenever `SPU_FP_NATIVE` is): the SPU thread's MXCSR also sets FTZ and DAZ. SPU single
    precision has no denormals (exponent 0 reads as zero, a result below the normal range is +0), so the native
    `fa`/`fs`/`fm`/`fma` no longer check their inputs for denormals; the exact path still covers exponent 255 and
    overflow. Double-precision helpers clear FTZ/DAZ with the rounding mode. `test_spu_fp_daz` (new): 480M lane results
    of six ops against the exact path, 0 differ; without DAZ in MXCSR the same test reports 17.9M differences.
  - Toolkit `SPU_LS_VEC` (SSSE3 x86 hosts): `spu_ls_read128`/`spu_ls_write128` are one 16-byte load or store and one
    `pshufb` word swap. The old form stored four byte-swapped words and reloaded them as a quadword, which stalls on
    store forwarding.
  - a67e replay (job lab, 300 jobs, 300/300 identical): ~2.17 -> 2.12 ms per job with `SPU_LS_VEC`, then ~1.50 ms
    with `SPU_FP_DAZ` (-29%).
  - Corridor A/B (x159, 360-690 s, order old new new old, host quiet): previous build 11.14 / 10.90, new build
    11.35 / 11.65 game frames/s (+4.4% on the means). Scenes match. The new build is the reference.
- **M0.86: BatchJobList's position-independent code lifted (relocatable lifts); the wait moves to General6 (2026-10-28).**
  - Toolkit: `spu_lifter.py --reloc` lifts position-independent code once at a canonical base; every address the
    code derives from its own pc (links, `lqr`/`stqr`, transfer and return pcs) adds `ctx->reloc_delta`.
    `spu_overlay_register_region_reloc()` makes a copy resident wherever the title DMAs it, and dispatch sets the
    delta. The four blobs are pc-getter PIC (`brsl $rX, .+4`; no absolute branches or `lqa`/`stqa`).
    `analysis/lift_reloc_blobs.py` lifts them (canonical base `0x20000`, images 900-903); `SPU_INTERP_MISS` now reports
    misses inside such code at the canonical pc, ready to pass back as entries.
  - Effect on the SPU side (x136): interpreted SPU time 13.6 -> 4.4 s per 20 s, BatchJobList 77% -> 29% busy, all the
    workloads 497% -> 449%. No hang and clean frame dumps in every run.
  - Effect on the frame (x144, one diagnostic exe, poll sites as spans, 420-480 s): with the lifts the main thread's
    wait on BatchJobList (`0x464F84`) fell from 9.55 to 4.00 s per 60 s, but its waits on General6 grew
    (`0x375A88` 6.68 -> 9.25 s); 10.81 vs 10.46 game frames/s. Corridor A/B (x142, quiet host, new vs previous
    build): 11.04 / 10.63 vs 11.22 / 11.64. The frame rate is neutral within noise, so the port keeps them off
    (`SPU_RELOC_OFF=1` default; `=0` runs them). General6 (job module a67e, `4AB0`) is the next bound.
  - Still interpreted in BatchJobList: targets outside the four blobs (`0x359C0` ~1.5 s per 20 s, `0x18BD0`,
    `0x20BD0`), code from other sources; not chased while General6 is the bound.
  - Timing notes from the same day are in DEBUGGING.md ("Before trusting a timing"): a full drive and a busy host
    each cost 25% or more, which an A/B between builds cannot tell from a regression.
  - The toolkit change went upstream as PR #636 (ported to v0.18 with a unit test; CI green).
  - New benchmark recipe (DEBUGGING.md: attacks stop at 300 s): the production build gives 11.70 / 11.72 game frames/s.
  - General6 on more lanes does not help (x149 vs x150, one diagnostic exe, new recipe, 420-480 s): with
    `3=6` General6 really runs on 4 SPUs 24% and on 5-6 SPUs 29% of its active time, yet the main thread's wait on
    it (`0x375A88`) grows 10.22 -> 11.48 s per 60 s and the frame rate falls 11.77 -> 9.53. Its job list has a serial
    chain: only faster jobs shorten it.
  - Where General6's time goes (x148, host sampler on wid 3 only): job module a67e `4AB0` 46%, a67e `63B4` 12.7%,
    job module 1bdd ~30% (`59C0` 8.1%, `53E0` 5.6%, `4450` 5.1%, `A588` 5.0%, `73E0` 2.7%, `40B8` 2.6%), a67e `4930`
    2.4%. `4AB0` is a ~2200-line lifted function of float math (231 `fma`, 178 `fm`, 176 `fa`) and shuffles (460
    `shufb`): the next native-stage candidate, by the EDGE_NATIVE.md recipe. The shuffle and rotate helpers already
    use `pshufb`.
  - Job lab on General6 (lab exe, `SPU_JOB_CAPTURE_WIDS=3 SPU_JOB_CAPTURE_IMG=218`): 300 a67e jobs captured, replay
    300/300 identical, ~2.2 ms per job. A block count of `4AB0` shows that only its entry block runs: 780 straight-line
    operations (113 indexed `lqx` gathers, 190 `shufb`, 80 `fa`, 44 `fm`), called 1920 times per job. Register
    promotion of that block gave no gain in replay.
  - Toolkit: `shufb` and the quadword byte rotates/shifts no longer swap bytes around `pshufb` (the index is remapped
    instead, SSE4.1 path). Exact: `test_spu_shufb` passes 200000 random cases. a67e replay 2.227 -> ~2.17 ms per job
    (-2.6%). Corridor A/B (x156, new recipe, 360-690 s): previous build 11.58, new build 11.62 game frames/s (the second
    pair is not counted: one run had a browser using about a core, the other ran during bursts of host system load). Neutral to slightly positive; the new build is the reference.
  - The walker (`ENG_PHASE_STATS`) now spends ~77% of its time in methods (was 67%).
- **M0.85: the same two lists as kernel-thread lanes, ~10.8 game frames/s in the corridor (2026-10-28).**
  - `SPURS_LANES` (the mechanism the EDGE lists already used) lets up to n of the six kernel threads run a workload
    at once, each as the SPU it plays (its own spuNum), with the rerun window kept and no thread created per
    dispatch. General6 and BatchJobList have a priority on SPUs 0-5, so every kernel thread may take them.
  - Port default now `SPURS_LANES=15,16,17=3;3,7=3`, with no `SPURS_MAXCONT` default. Corridor, 360-690 s:
    - lanes vs `SPURS_MAXCONT=3=3,7=3` (x131, M L L M): 10.82 / 10.91 vs 9.03 / 8.60 game frames/s;
    - adding `9,10,11,13` to the lane group (x132, L P P L): 10.82 / 10.64 vs 10.63 / 10.77, noise, not adopted.
  - From ~7.4 before M0.84 that is about +45%. No `[pm-hang]` and clean frame dumps in all eight runs.
  - 25-minute soak with the new default (x133, environment unset): no hang, no `[pm-hang]`, longest gap between
    flips after the boot 4.8 s at ~280 s. From ~250 s on, this run's bot fought in another part of the area (by
    the stairs, more enemies on screen) than the door the A/B runs stood at, so its rates (9.56 at 360-690 s, 10.01
    at 690-1100 s, 10.28 to the end) are of a different scene. The pad script is timed in wall-clock, so where the
    bot ends up can differ between runs: compare A/B arms only while their frame dumps show the same place. All
    eight x131/x132 runs were at the door at both 360 s and 690 s (the dumps nearest those times, picked by file
    time).
  - Frame timeline after the change (x134, poll sites as spans again, 420-480 s at the door, 10.65 game frames/s):
    the main thread is blocked ~66 ms of a ~95 ms frame (41.4 s in 630 frames): `0x416A44` 10.7 s, `0x464F84`
    9.1 s, `0x3E8910` 7.3 s, `0x416654` 7.3 s and a new one, `0x375A88`, 4.9 s (General6 busy on 2.3 SPUs meanwhile).
    During every one of those waits 5.0-5.7 of the 6 SPU threads are busy, and `SPURS_PM_PROF` (x135) puts the
    workloads at 497% of 600% with 2% idle: the SPU pool is now saturated. (M0.86 below: cutting BatchJobList's SPU
    time shortened its wait, but the frame then waited on General6 instead, so the frame rate barely moved.)
  - Where that time goes (x135, `SPU_INTERP_MISS`): General6 95% busy and 0% interpreted (it is lifted; most of it is
    job module a67e's `4AB0`), the three EDGE lists 62% each and 0% interpreted, the audio workload 66% on its own
    SPU, and BatchJobList 77% busy and **87% interpreted**: ~13 s of every 20 s of interpreted SPU time, nearly all
    the interpreter time left. Its code comes from four EBOOT blobs (`0x716730` 0x2450 bytes, `0x6D66B0` 0xB80,
    `0x6AC8B0` 0x28E0, `0x6AF1B0` 0x32E0) that its jobs copy to a new 16-byte-aligned LS address at each load (x112:
    299 loads, 299 different addresses), so a lift per placement cannot cover it; a position-independent lift can.
- **M0.84: General6 and BatchJobList run on three SPUs each, +19% in the corridor (2026-10-28).**
  - The main thread's longest waits in the corridor frame (x125: poll sites traced as `PS3_FTRACE` spans with a
    temporary lift patch) are `0x464F84` (41 ms of a 125 ms frame, while wid 7 BatchJobList runs) and `0x3E8910`
    (33 ms, General6), then `0x416A44` (15 ms) and `0x416654` (12 ms). During the `0x3E8910` wait only 2.15 SPUs are
    busy, and every non-EDGE workload runs on exactly one SPU at a time: the title registers maxContention 1 for all
    of them.
  - `SPURS_MAXCONT=3=3,7=3` (port default until M0.85 replaced it with `SPURS_LANES`) runs those two lists on up to three virtual SPUs, each with its own
    spuNum (lanes 1-2 on worker threads). Corridor, 360-690 s:
    - `3=3` alone vs off (x126, x127): 8.01 / 8.21 / 8.12 vs 7.45 / 7.28 / 7.50 game frames/s;
    - `3=3,7=3` vs `3=3` (x128, x129): 8.70 / 9.07 / 8.93 / 9.16 vs 8.31 / 8.18;
    - `3=6` (clamped to the instance's SPUs) is no better than `3=3` (8.55 / 8.07);
    - adding `9,10,11,13=3` is much slower (5.75 / 6.04): a lane run creates its worker threads at every dispatch
      and skips the rerun window, which costs more than it gives on short, frequent lists.
  - No hang in any of the 14 runs; the frame dumps show the corridor and combat drawn as before. The lanes claim
    jobs through the job list's GETLLAR / PUTLLC atomics; a lost update there would leave a count above zero and
    hang the main thread's poll, which no run showed.
  - 25-minute soak with the new default (x130, environment unset): 8.90 game frames/s at 360-690 s, 8.67 at
    690-1100 s, 8.96 to the end; no gap between flips beyond the ~140 s load; frames clean.
  - Next on the same path: persistent lane workers (no thread per dispatch, rerun kept), which may also make the
    shorter lists worth a lane.
- **M0.83: the lifted PPU code builds for x86-64-v3, +2.5%; the corridor frame is main-thread bound (2026-10-28).**
  - Frame timeline of the corridor (x117, `PS3_FTRACE` 420-480 s, re-lifted exe): median frame 123.9 ms. SPUs 0-3
    are busy ~60% of it, SPU 4 87% (wid 8, the audio workload, alone on it; not on the frame's path), the walker
    drains for 85.7 ms. Inside each frame there is a ~40 ms stretch where SPUs 0-3 are idle and the walker only
    drains in short bursts (`D D D D`): only the main thread works then, so it is the frame's critical path.
  - The main thread's 22.7% at `0x3E8900` is a poll of a u16 at `[[r30+0xC]+0xE0]` (loop `0x3E8910`) after the
    frame routine's calls; `0x416A44` and `0x464FA4` are other polls (main-prof ~8% and ~18%). Which writer clears
    them is not known yet.
  - The `0x3E8900` poll, followed through (x119-x123): `[0x9397F8]` = `0x80FF8CA0`, the same in three dumps, so
    the polled u16 is at `0x80FF8D80`. A 1 ms byte-change watch (`GOW_LLE_WATCH=1 GOW_LLE_DUMP_EA=80FF8D00`) shows it
    set to N and counted down to 0 (~110 times a second): a job list's remaining-job count. `SPU_ATOM_EA=80FF8D80`
    names the writer: PUTLLCs from the WWS policy module (image 10, pc `0x2628`). `SPU_ATOM_WIDS=60` puts every
    atomic on that line in wid 3 (General6). So the main thread waits for General6 to finish its list, ~28 ms a
    frame. General6 spends 61% of its time in job module `a67e`'s `4AB0` and 15% in `63B4` (earlier profile).
    `GOW_GUARD=80FF8D80` reported no write at all on that page, though the PPU and the SPUs both write it.
  - `PPU_HOST_ARCH` (port CMake, default `-march=x86-64-v3` like `SPU_HOST_ARCH`, with `-ffp-contract=off` since FMA
    then exists): corridor A/B in exes g f f g f g (x118), 7.45 / 7.63 / 7.55 against 7.44 / 7.25 / 7.39 game
    frames/s, +2.5% mean, the new build ahead in all three pairs. `-DPPU_HOST_ARCH=` brings back the SSE2 baseline.
- **M0.82: real gameplay runs at about half the benchmark rate; the corridor's EDGE fragments lifted (2026-10-27).**
  - The benchmark scene (360-720 s) was the Megaera dodge tutorial: the pad script only pressed X, so Kratos kept
    getting hit and the game never moved on. With the right stick flicked every 400 ms (`PAD_STICK` 7th field,
    toolkit 2f614c87) the dodge passes, and pressing square / triangle / circle about three times a second through
    `PAD_FILE` (`scripts/pad_masher.sh`) passes the attack tutorial and the prison. The game then reaches a corridor
    in front of a door at ~270 s and stays there.
  - In that corridor the game ran at ~6.3 game frames/s (~10 presents/s), against ~12.7 in the tutorial loop. The
    README's number was the tutorial's. The corridor (360-690 s) is now the benchmark scene.
  - What differs there (x110, x111): the RSX walker was ~67% busy (20 s of methods in 30 s; ~17.6k draws/s), and a
    third of the sampler's hits were in the SPU interpreter. `SPU_INTERP_MISS` put 37.5 s of interpreted SPU time in
    every 20 s: the three EDGE workloads were ~82% busy each with 62% of it interpreted (EDGE code fragments at
    placements no earlier run had captured), and wid 7 (job module 759d) was 48% busy, 88% interpreted.
  - A `SPU_LSA_GETLOG` capture in the corridor (x112) gave 131 new EDGE placements; `lift_edge_frags.py` now has
    569. The script now passes the function entries of an existing lift again (a re-lift from new logs dropped the
    entries that came from older logs) and keeps the lift the port patches at build time (`edgefrag_7228c0_11150`)
    as it is.
  - After the re-lift (x114): EDGE workloads ~42% busy each with 0% interpreted, all SPU kernels 4.57 -> 3.54
    cores, interpreted time 37.5 -> 6.8 s per 20 s (what is left is wid 7). Frame rate, corridor A/B e d d e
    (x113): 7.44 / 7.08 against 6.93 / 7.03 game frames/s, +7.4% and +0.7%. A core of SPU time went away and the
    frame rate barely moved, so the SPUs are not what limits the corridor.
  - wid 7's code is staged by the job itself: EBOOT blobs (`0x716730`, `0x6D66B0`, `0x6AF1B0`, `0x6AC8B0`, up to
    `0x32E0` bytes) copied to LS addresses that change from job to job (299 placements in one run), so lifting one
    copy per placement does not scale. It is ~0.33 cores interpreted.
  - Upstream PR #619's lost-wakeup fix (toolkit b606ff7c, `SPU_WAKE_LOCK=0` skips it): corridor A/B w n n w (x116)
    7.09 / 7.11 against 6.75 / 7.21, noise. Kept on as a correctness fix.
  - The corridor's limit (x115, re-lifted exe, 6.9 game frames/s): the main thread at ~0.96 cores with 22.7% of its
    samples in the frame routine's wait at `0x3E8900` (22.0% in x110, before the re-lift), the walker ~68% busy, the
    SPU kernel threads ~0.55 cores each. Nothing is saturated, so the next step is a frame timeline (`PS3_FTRACE`) of
    the corridor: who waits for whom within a frame.
- **M0.81: the walker's pipeline lookup without the linear scan, +4% (2026-10-27).**
  - `eng_pipeline_get` runs on every draw (~2000 a frame). It compared the key with every cached pipeline in turn,
    and walked the fragment program four times: sampled units, gamma units, sign-expansion units and the
    structural hash.
  - A key-hash index over the pipeline list finds the same entry the scan did (a key is added only when it is not
    found, so keys are unique). A 16-entry memo, validated by a copy of the program's bytes like the existing
    vertex-program memo, keeps the three unit sets and the structural hash per program; the per-draw texture
    masks are applied to the stored sets. Inline constants are still collected every draw (toolkit ae236f41).
    `RSX_PIPE_FAST=0` turns both off.
  - A/B, one exe, env alternated f n n f (x104), game frames/s over 360-720 s: 11.81 / 13.27 against 11.55 / 12.53,
    +2.3% and +5.9%, no hang, nothing else above 0.2 cores. The spread between the two pairs (~1.3 frames/s) is
    larger than the effect, so the size is uncertain; the lookup returns exactly what the scan returned, so it is
    kept.
  - Where the walker's driver time goes (x105, `PS3_SAMPLE_MOD=nvoglv64.dll`): the NVIDIA Vulkan driver is at most
    ~24% of the walker's samples, and 79% of the driver samples are entered from `vk_eng_draw`. Each draw began and
    ended its own render pass behind a full memory barrier and an image barrier per target; `VK_DRAW_PROF` put
    recording at ~5.7 us of the draw.
  - Tried, not adopted as a default: `VK_PASS_MERGE=1` (toolkit e5a4814c) keeps a batched draw's render pass open
    for the next draw with the same render pass, framebuffer and size that samples none of the pass's targets.
    96% of draws went into an open pass, no draw sampled a target of its own pass, recording fell to 2.2-2.4 us,
    and the frame dumps looked right (x106). A/B f n n f (x107): 12.75 / 12.73 against 12.64 / 12.40, +1.8%,
    under the 0.44 noise floor, with a browser using ~0.5-0.7 cores in three of the runs. The walker's own CPU
    fell from ~0.86 to ~0.78 cores without the frame rate following, so after M0.80 and M0.81 the walker is no
    longer the only limit. The knob stays, off.
- **M0.80: EdgeWatchdog sleeps instead of spinning, +5.8%; the RSX walker is the binding stage (2026-10-27).**
  - Per-thread host CPU over 120 s (x101, `Get-Process` thread times, tids matched to the all-thread sampler): main
    thread 0.91 cores, RSX FIFO walker 0.87, the busiest SPU kernel thread 0.85, the others 0.73-0.77, and one
    above-normal-priority thread at 0.77 cores that the sampler did not cover. Guest threads with priority below 256
    get `THREAD_PRIORITY_ABOVE_NORMAL`.
  - That thread is EdgeWatchdog. It polls with ~800 us `sys_timer_usleep` calls, and the runtime serves a sleep
    under 1 ms with a `QueryPerformanceCounter` loop around `SwitchToThread`, so the watchdog held a logical
    processor for the whole sleep. `PS3_USLEEP_WAIT_US=<n>` (toolkit fd12038a) waits a sub-millisecond sleep of
    at least n us on a high-resolution waitable timer instead; shorter sleeps, like the main thread's 30 us polls,
    keep the precise spin. The port sets 500.
  - The walker: in the x99 traces its drain passes cover 57.8 of 60 s with no ACQUIRE block, and the flip reaches
    it ~23 ms after the guest requests it. `ENG_PHASE_STATS=30` (x102) shows a constant ~22.9 s of every 30 s in
    methods, 20.3 s of it in 580-700 thousand draws (~2000 per frame, ~30 us each): backend recording 6.2 s,
    textures 5-6 s (hash 1.4, re-upload 1.5-1.8, surface stores 0.9-1.7), vertex fetch 4.3 s, pipeline lookup
    3.5 s, index build 0.7 s.
  - Causal check: `RSX_SLOW_PCT=<n>` (toolkit 5f4740df, diagnostic) busy-waits n% of each walker pass. At 25 the
    game ran 10.65 game frames/s against 12.21 / 12.39, -13% where a fully binding stage would give -20%.
  - A/B, one exe, env alternated b s w b w (x103), game frames/s over 360-720 s: 13.02 / 13.02 with the watchdog
    timer against 12.21 / 12.39, no hang; the game used ~6.6 cores against ~7.5, and nothing else used more than
    0.5 cores during the runs.
  - Next: per-draw work on the walker (pipeline key, vertex fetch, texture checks, backend recording).
- **M0.79: indirect-branch dispatch cache, dispatch share 14.1% -> 11.6%, frames/s within noise (2026-10-27).**
  - Most indirect branches on the render/job chain go to a few targets: in an x84 census of ~700k branches/s, one
    target was 12% and the top 24 were 44%. Each one still walked every check in `spu_indirect_branch`, up to
    16 resident code slots, and up to three `spu_lookup` probes into a 96 KB per-thread table that the lifted code
    between dispatches evicts.
  - A per-thread cache of 512 16-byte entries, keyed on pc, image, overlay and policy mode, now answers the common
    case (toolkit 4f4c1418, 7934d504). An entry is written only when the standard lookups found the target and
    nothing else applied; job entry and the resident code slots are checked live on a hit, and the paths that read
    local store are never cached. Any function, task-entry or task-ELF registration bumps one epoch that empties
    the cache. `PS3_SPU_IBC=0` turns it off, as does any trace switch the dispatcher reads.
  - A bitmask of the resident code slots in use lets dispatch skip the 16-slot loop when no slot is in use. The
    `spu_lookup` entries shrank from 24 to 16 bytes, so the table is 64 KB and no entry straddles two cache lines.
  - The cache alone did not help. In a host profile (x94) the cost moved from `spu_lookup` into
    `spu_indirect_branch`, and the dispatch group stayed at 14.3% (14.1% before): the slot loop and three global
    loads for the generation still touched cold cache lines. With the slot mask and the single epoch (x95) the group
    is 11.6% (`spu_indirect_branch` 6.4%, `spu_lookup` 1.1%, `spu_drain_call` 4.1%), and `spu_overlay_note_get`
    fell from ~1.3% to 0.8%. On this host, cost per dispatch follows the cache lines it touches, not the
    instructions it runs.
  - A/B without `SPURS_PM_PROF` (x96, alternating, one instance at a time), game frames/s over 360-720 s: 12.22 /
    12.11 against 12.17 / 11.92 for M0.78, +0.4% / +1.6%, inside the noise floor. No hang. Nothing else used more
    than 10 CPU-seconds per 30 s during the runs.
  - The per-frame ftrace numbers look larger than that and are not a result: SPU busy 360.3 / 357.4 against
    397.1 / 380.6 ms per frame and drain 75.4 / 76.8 against 85.7 / 82.4, but the 20 s trace window held 250 / 251
    flips against 222 / 233, and the total SPU busy time in it is the same in both builds (90.1 / 89.7 against
    88.2 / 88.7 s). The heartbeat for the same stretch does not show the extra flips. A per-frame
    number from one 20 s window moves with the flip count, so compare window totals when the counts differ.
  - The build is kept for the lower dispatch cost (x95) and because it is not slower; it does not move frames/s.
  - Tried after it, not adopted: pinning each SPURS kernel thread to its own physical core. The game uses ~6.5 of the
    host's 12 logical processors (6 cores), so kernel threads share cores with SMT siblings. `SPURS_PIN=1` (toolkit
    branch gow-v017pin, off by default) gives kernel thread k the first logical processor of core k. One exe, env
    alternated (x97): 11.43 / 11.50 game frames/s pinned against 11.91 / 12.04 unpinned, -4% in both pairs, no
    hang. A hard pin keeps a kernel thread waiting when the main, RSX or Vulkan thread holds its processor, where
    the scheduler would have moved it.
  - Also tried, not adopted: letting the main thread's wait loops yield. A host sample of every thread with the main
    thread's guest pc (x98, `GOW_MAIN_PROF`) has the main thread on a processor almost all the time, about three
    quarters of it in three polling loops: 0x3E8910 in frame routine 0x3E82A8 (reload a halfword flag, 16 nops,
    loop, ~13%), and the job-slot scan 0x416338 called from loops 0x416654 and 0x416A44 (~25%), plus a bounded
    poll at 0x464F80 (~15%, not hooked). A probe called a hook at the three loop heads that paused for the first
    64 quick repeats and then called `SwitchToThread`. Game frames/s over 360-720 s, one exe, env alternated
    y n n y y n (x100): 11.42 / 12.02 / 10.39 yielding against 12.53 / 12.00 / 11.53, no hang. The pair with no
    other load on the host is even; in the other two the yielding run had a browser or chat client using ~0.8-1.1
    cores and lost more. The game used ~7 logical processors in both arms. With the pinning result, two tests say
    host CPU contention is not what limits frames/s; the spin cycles freed did not turn into SPU progress. The
    probe was removed.
- **M0.78: MFC parameter channel writes skip the MFC path, +1-2.7% (2026-10-27).**
  - A host profile of the critical render/job chain (x91, SPURS workloads 2 and 9-13) puts the runtime at ~36% of
    samples, with ~18% in channel/MFC code (`spu_wrch_impl` 3.3%, `mfc_for` 2.5%, `mfc_submit` 2.2%) and ~14% in
    indirect-branch dispatch (`spu_indirect_branch` 6.6%, `spu_lookup` 3.9%, `spu_drain_call` 3.8%).
  - Five or six of the eight or nine channel writes of every DMA only latch LSA, EAH, EAL, size, tag or the tag
    mask. `spu_wrch` now stores those in the context directly instead of going through `spu_wrch_impl`,
    `mfc_for` and the channel switch (toolkit b0c9b2c8, c1720f75). `SPU_CHHIST` turns it off.
  - The first A/B (x92) measured nothing: the fast path was also off under `SPURS_PM_PROF`, which the A/B harness
    always sets. It did show the noise floor: the two M0.77 runs differed by 0.44 game frames/s (11.66 / 11.22).
  - Game frames/s over 360-720 s, alternating, one instance at a time (x93): 12.06 / 11.84 against 11.74 / 11.72
    for M0.77. SPU busy time 391.3 / 358.3 against 397.0 / 391.3 ms per frame (window totals 91.6 / 93.5 against
    94.5 / 94.7 s), drain 81.5 / 71.9 against 77.8 / 76.2. No hang. The CPU log shows nothing else above 13 CPU-seconds per 30 s during the runs.
  - Both arms ran with `SPURS_PM_PROF=60`, which times every channel write with two `QueryPerformanceCounter`
    calls in the old build; the fast path returns before that. Without `SPURS_PM_PROF` (normal play) the saving is
    smaller than measured. The A/B metrics come from `PS3_FTRACE` and the heartbeat, so later channel-path A/Bs run
    with `NOPMPROF=1`.
  - The main thread waits on the SPU chain for 71-82 ms of each 76-87 ms frame, so only work that shortens that
    chain shows up in frames/s; a trim smaller than ~3% cannot be told apart from noise with two pairs.
- **M0.77: GETLLAR without the lock-line lock for registered holders, +0.5-2.6% (2026-10-26).**
  - The last v0.18 SPU commit held back from M0.76 (c9a450d8): a context that already holds a reservation and
    re-reads a line that is already marked publishes its reservation and copies the line without taking the
    lock-line lock. Line marks are never cleared, so every repeated policy-module poll of the SPURS instance takes
    this path (no counter was added; read from the code).
  - Game frames/s over 360-720 s, alternating, one instance at a time (x89): 11.73 / 11.71 against 11.43 / 11.65
    for M0.76. The second pair is inside the spread of two M0.76 runs (0.22). SPU busy time is 10 ms per frame lower
    in both pairs (410.6 / 390.6 against 421.0 / 400.9), drain 82.0 / 77.3 against 85.1 / 79.9. The CPU log shows
    nothing else above 13 CPU-seconds per 30 s during the four runs.
  - It changes lock behaviour, so it also ran 25 minutes on its own (x90): no hang, 11.6-11.9 game frames/s per
    two minutes from 720 s on, working set 1127 -> 1156 MB and private bytes 2394 -> 2462 MB between 300 and 1500 s.
    From 240 to 600 s it ran at 10.0-10.4 while I was running git cherry-picks in the toolkit tree; the CPU log
    shows no other process above 8 CPU-seconds per 30 s then, so the cause is not confirmed.
- **M0.76: ps3recomp v0.18 SPU and runtime work picked onto the toolkit, +5% (2026-10-26).**
  - ps3recomp v0.18.0 is out (2026-10-26). Our 56 toolkit commits conflict with it in about 140 hunks over 26
    files, so the full merge waits; the commits that matter for speed were picked onto `gow-v017` instead:
    poison scan only when reported (a903520e), lwmutex with an atomic lock word (dc37f00b), a leaf-import fast
    path (9a3fbffd), buffered stdout/stderr (c40829d7), the walker forgetting a park (3e9311c6), a bounded EA->IO
    lookup (06a37689) and SPU_DIRECT (#510): a lifted branch with a known target tail-calls it instead of
    returning to the drain loop.
  - The SPU_DIRECT commit also makes a drain restore the outer `drain_ret_pc` when it returns. Here that hung the
    level after about 2.5 minutes (`[pm-hang] wid=12 pc=0x02EA0 img=10`, RenJobSet's policy module) with or without
    SPU_DIRECT on; this toolkit's interpreter still stops at `drain_ret_pc`, which v0.18 changed. Setting it back
    to 0 when a drain returns (3c818525) fixes it.
  - Lifts made before this need the new tails: `python scripts/spu_direct_rewrite.py port/spu --apply` rewrites
    33,953 sites in 364 images (a fresh lift with the toolkit's `spu_lifter.py` emits them already).
  - Game frames/s over 360-720 s, alternating, one instance at a time (x88): 11.41 / 11.51 against 10.91 / 10.90.
    Every per-minute rate of the new build is at or above the old build's. SPU busy time drops from 450-465 to
    400-419 ms per frame over six SPUs. No hang in four 12-minute runs. The CPU log was not running for this
    batch.
  - Held back: the GETLLAR fast path (c9a450d8) was picked, then reverted while the hang was bisected; it was not
    the cause and is M0.77. Per-line PUT copies (13a7676b) would break the PUT ordering this
    game depends on (`SPU_PUT_HEAD_LAST`).
  - `PS3_SPU_DIRECT=0` goes back to the trampoline.
- **M0.75: the GPU depth path pays with six kernel threads, +4% (2026-10-26).**
  - With the sixth kernel thread the main thread waits on the walker again, so `RSX_Z2ARGB=1` (M0.73) was
    measured again and is now the port default.
  - Game frames/s over 600-960 s, one instance at a time: on 11.06 (x81) and 10.75 (x82), off 10.33 and
    10.31 (x82, run first and third). The walker's sync GPU wait drops from 7-10 to 4.5-5.6 ms per frame, and
    drain from 85-98 to 77-86. Frames over 150 ms in the trace window are no more frequent with it on.
  - Four more runs were slow in stretches (per-minute rates down to 7-8) and are discarded. For two of them the
    CPU log shows other work on the host at those times: a WMI query of my own (x81n_2) and another session's
    long `grep` (x82z_2). For x81n_1 and x81z_2 the CPU log was not running.
  - `RSX_Z2ARGB=0` goes back.
- **M0.74: a sixth SPURS kernel thread, +1-2% (2026-10-26).**
  - SPU 4 was the last SPU without a kernel thread; HighestPrio6 and General6 have nonzero priority there. The
    port now sets `SPURS_KERN_SPUS=0,1,2,3,4,5` (was `0,1,2,3,5`).
  - Alternating, one instance at a time, game frames/s over 600-960 s (x80): 10.59 / 10.61 against 10.38 /
    10.49. Per frame: HighestPrio6 wait 11 -> 9 ms, flip pacing (`375A88`, main-thread profile) 7-8 -> 10 ms:
    the main thread waits on the walker again. SPU busy 420-447 -> 456 ms per frame over the six. No hang,
    frame dumps unchanged. Eight threads are busy on a 6-core/12-thread host.
  - Per workload, SPU busy per frame (x78n_1, five threads): General6 69 ms, the audio workload
    (`BrBDblBufWrkld`, SPU 5 only) 60, each EDGE lane 58, RenderJobs 32, BatchJobList 18, RenJobSet 11/12
    18/12. The audio share is a fixed ~67% of wall time on its SPU, not per frame.
  - `SPURS_KERN_SPUS=0,1,2,3,5` goes back.
- **M0.73: depth read as A8R8G8B8 built on the GPU: exact, but no frame-rate change (2026-10-26).**
  - The largest `z2guest` case is the 1280x720 D24S8 zeta at `0x011B0000`, sampled twice per frame as a linear
    A8R8G8B8 texture (the deferred passes rebuild positions from it). The walker stored it to guest memory (a GPU
    wait, a CPU conversion, an upload) each time. No CPU or SPU reads those bytes: an SPU GET watch over the
    region and a PPU read watch on the zeta saw none.
  - Toolkit `RSX_Z2ARGB=1` builds the texture on the GPU instead. A compute shader packs the depth and stencil
    aspects into the texels the guest word decodes to, and the result is cached until the depth changes.
    `RSX_Z2ARGB=2` also does the store and compares: 0 differences over ~17k compares in a 12-minute run (x78chk).
    The first version differed only where depth was 1.0, because the NVIDIA driver reassociated the shader's
    rounding (0x1000000 instead of 0xFFFFFF). The math is now `precise`.
  - `ENG_PHASE_STATS` (x79z against x76prof): z2guest falls from ~75 to ~0.5 ms per second of play, and texture
    resolve from ~230 to ~150 ms per second. In the frame trace, the sync GPU wait goes from 7.3-8.5 to 5.3-5.5 ms
    per frame.
  - Game frames/s over 600-960 s: 10.40 on against 10.48 and 10.35 off (x78). The second "on" run is discarded
    because it overlapped my own trace analysis (9.46, with a 3-minute dip that matches the analysis).
  - The frame trace explains the null result. The SPU rows are solid while the RSX row has idle gaps, and the
    walker is busy ~70% of the time with or without the change. The frame is SPU-bound, so the walker time saved
    only waits longer.
  - With it on, 8-11 frames over 150 ms appear at 613-618 s, in all three traces, against 0-1 off. In those
    frames the RSX row is a GPU wait. The new path itself takes at most 4 ms a call and created 2 textures in
    the whole run, so the cause is not yet known.
  - Off by default (the port does not set it). Next: the SPU side, the runtime's own dispatch cost (~20-29% of
    SPU time).
- **M0.72: a fifth SPURS kernel thread, +4-11% (2026-10-26).**
  - With three EDGE lanes the four kernel threads were ~100% busy (x71: SPUs 99-103% in the trace), and the
    main thread's two SPU waits were the frame's slack: the EDGE list (`3E8388`, ~7 ms per frame) and
    HighestPrio6 (`467B2C`, ~14). The port now sets `SPURS_KERN_SPUS=0,1,2,3,5` (was `0,1,2,5`): SPU 3, where
    EDGE0-2 have priority 8, HighestPrio6 2 and General6 12.
  - Alternating, one instance at a time, game frames/s over 600-960 s (x74, x75): 9.77 / 10.02 / 10.12 against
    9.37 / 9.01 / 9.09. Per frame: EDGE wait 7 -> 0 ms, HighestPrio6 wait 14 -> 11, flip pacing (`375A88`)
    6-7 -> 10-13 (the main thread now waits on the walker). Seven threads are busy on a 6-core/12-thread host;
    the walker's drain grew in one pair (115 against 93 ms) and not in the other two. Frame dumps unchanged, no
    hang.
    `SPURS_KERN_SPUS=0,1,2,5` goes back.
  - The heartbeat's `presents` are not distinct frames: `flips` equals `game_frame` in every run, while presents
    run 1.4-1.8x higher. The frame clock (`main.cpp`) presents on the flip-pending flag and again when the flip
    count changed, so one flip can be presented twice. Game frames/s is the frame rate; earlier presents/s
    figures in this file count those repeats.
  - Next: the walker is the bound again. Its largest known cost is depth and colour targets sampled with
    another format, copied to guest memory and uploaded again (`z2guest`/`s2guest`/reupload in
    `ENG_PHASE_STATS`, ~19 ms per frame).
- **M0.71: EDGE on three SPUs at once, +65%; the 3D scene the right way up (2026-10-25).**
  - Parallel EDGE lanes are on by default: the port sets `SPURS_LANES=15,16,17=3` (toolkit `SPURS_LANES`, see
    the lanes entry under M0.62 for how it works and why the old blocker was our runtime). One instance at a
    time, 600-1080 s, alternated (x63): off 5.63 / 6.20 game frames/s, two lanes 8.41 / 9.45, three lanes
    9.65 / 9.76; presents/s 11.05 / 12.28 off, 13.51 / 13.72 with three. No `[pm-hang]` or HALT in any run, frame
    dumps clean. `SPURS_LANES=off` goes back to one EDGE lane.
  - The 3D scene was upside down under an upright HUD (seen in the frame dumps). The fragment translator read
    WPOS as the host position (origin top left) and ignored `SET_SHADER_WINDOW` (0x1D88); GoW sets origin BOTTOM
    and builds its full-screen pass UVs as `wpos * (1/W, -1/H) + (0, 1)`, so every such pass mirrored its
    source. WPOS now follows the window register (height when origin BOTTOM, pixel-center bias), passed in the
    unused lanes of `fp_alpha`. Check run x64 (both defaults on): scene upright with the HUD upright, no hang,
    9.64 game frames/s and 13.9 presents/s at 600-1080 s. 127-174 of the game's fragment programs read WPOS,
    so screen-space reads in materials changed too; nothing wrong seen in the dumps, not checked further.
  - **Where the frame goes with three lanes** (x65: production defaults, one instance, 1100 s, PM profiling on,
    `PS3_FTRACE=600:20`), against x56 (six stages, one lane). Tail 5.95 -> 9.06 game frames/s (168 -> 110 ms
    per frame). The main thread's EDGE list wait (`3E8388`) fell from 83 to 6 ms per frame (49% -> 5% of
    samples): EDGE no longer bounds the frame. EDGE w15-17 busy 160% of wall (lanes overlap); ms/job
    0.0995 -> 0.1105 (+11%: the lanes share host cores). Timeline 600-620 s: 166 frames, median 112.8 ms; the
    RSX walker's drain is 110.5 ms per frame (of which GPU waits 9.4), i.e. the walker runs the whole frame,
    and the main thread is blocked only 17.7 ms per frame (flip pacing `375A88` 11.7). The frame is now bound
    by the walker's CPU time (~101 ms) and the main PPU thread (~95 ms busy) together: speeding up only the
    walker would land the frame on the main thread at ~100 ms. The four kernel threads (SPUs 0, 1, 2, 5) show
    100% busy; whether EDGE lanes spin at the ring-space wait is not known yet. The overlay stage O11430 stays
    default-lifted: with EDGE no longer the bound, an A/B of its ~1.9% of job time would measure noise.
  - **Host profile with three lanes** (x66: `PS3_SAMPLE=2 PS3_SAMPLE_HOST=1`, 1100 s; resolved with
    `samp_resolve.py`). The lanes do not spin at the EDGE ring-space wait: no samples at `B700`/`409C` on any
    kernel thread, `mfc_do_transfer` 1.7%. Six threads are busy nearly all the time on the 6-core host: main
    20.3% of samples, the four kernel threads ~16% each, the walker 13.1%.
    - Main thread: `vm_write64` 18.1% (the out-of-line store in `ppu_loader.cpp`; `vm_write32` is 0.5% of the
      whole process), guest `466E38` 14.8%, `416338` 14.7%, `416610` 8.7%, `3E82A8` 6.4%, `ppu_trace_enter`
      4.6% (the lift was made with `PPU_TRACE_FUNCS=all`).
    - Kernel threads: job module `a67e` `4AB0` ~9% each (not EDGE: absent from cap_3), runtime dispatch
      (`spu_indirect_branch`, `spu_drain_call`, `spu_lookup`) ~20%.
    - Walker: `rsx_vertex_remap_build` 18.1%, `rsx_draw_engine_guest_read` 10.8%, texture content hashes ~8%.
  - **How to measure from here.** With the walker (~101 ms CPU) and the main thread (~95 ms busy) both near the
    frame, a change on one side moves frames/s by at most ~12% before the other side bounds it. A one-sided
    change is gated on its own thread's cost from one ftrace window: walker CPU = drain - GPU waits, main busy
    = frame - main blocked (plus `VK_DRAW_PROF` us/draw for the walker); frames/s is claimed only when both
    sides have come down.
  - **Inline lifted stores and a gated trace hook** (toolkit `ppu: inline fast path for lifted stores`). Every
    lifted store called the out-of-line `vm_writeN` (the lift's loads were already inline); the lift header now
    stores inline when no store diagnostic is armed (`vm_inline_st_ok`, from the variables that arm the
    watches and store observers), the address is plain guest memory and its line is not SPU-reserved, and
    otherwise calls the out-of-line store as before. `ppu_trace_enter` runs only while it can still log
    (`ppu_trace_live`). The existing lift (`work/recompiled_v015`) was not re-lifted: its `ppu_recomp.h` was
    replaced by the new template preamble plus its original forward declarations (the template's `vrsave`
    field left out, as the v015 sources were lifted without it). x68 against x65 (one run each, same setup):
    frame median 112.8 -> 106.1 ms; main thread busy (frame - blocked) ~95 -> ~88 ms; walker CPU (drain - GPU
    waits) 101.1 -> 95.8 ms; 600-1080 s 9.06 -> 9.27 game frames/s, 14.25 -> 14.40 presents/s; no hang.
  - **Walker per-draw work** (toolkit `rsx: cheaper per-draw walker work`; switches in DEBUGGING.md, all default
    on). Baseline per 10 s (`ENG_PHASE_STATS`/`VK_DRAW_PROF`): ~170k draws; per draw fetch ~9 us, pipeline
    ~5.5, textures ~15, backend ~10.5. Causes found: the vertex remap probed a hash table sized for the largest
    draw ever seen; the guest readback counted non-zero pixels for the pass dump even with no dump running;
    the pipeline key re-hashed the vertex program byte by byte every draw; the texture content hash was one
    serial multiply chain. Fixed without caching anything across guest writes (no write tracking exists).
    x69 (on) against x69off (all four off), same exe, one run each: walker CPU (drain - GPU waits) 88.5 vs
    96.2 ms per frame (x68 baseline 95.7); main thread busy 89.8 vs 86.5; frame median 105.3 vs 105.0 ms. The
    frame did not move: it is now bound by the main PPU thread (~87-90 ms busy). Frame dumps unchanged; no
    hang. Still on the walker: depth/colour stored to guest memory and re-decoded as textures (~18 ms per
    frame with GPU waits), FIFO decode (~16% of walker samples), `rsx_vertex_fetch_many` (6.4%).
  - **The main thread's HighestPrio6 wait is SPU work, not scheduling** (x70, x71; one instance, 3 lanes,
    `PS3_FTRACE=600:20`). The top main-thread guest function `466E38` (called from `467B18`) busy-waits on the
    per-SPU progress counters of the `HighestPrio6` job list (wid 2, priority 3 3 3 2 1 1): ~13-15 ms per frame,
    counted as main busy. Two scheduler changes left it unchanged:
    - `SPURS_RERUN_YIELD=1` (toolkit, default off): end a module's rerun window when a workload the SPU ranks
      higher is ready. It fired (~10k ended reruns a minute) and changed nothing: x71 game frames/s 600-1080 s
      9.27 against baseline 9.36 / 9.34, HP6 wait 15 against 14 ms per frame.
    - `SPURS_PM_RERUN_US=500` instead of 2000: 9.30 / 9.34 game frames/s, HP6 wait 14 ms.
    - The trace shows why. HighestPrio6 runs once per frame, at the end (median start 99 ms after the flip,
      7 spans, 2.1 ms busy, one cluster on one SPU). In the 15 ms before it starts the four kernel threads are
      busy throughout: General6 (wid 3) 11.8 ms, RenJobSet (wids 11, 12) 16.2, EDGE (15-17) 22.2, audio (8) 7.7.
      The list becomes ready only after the SPU work it depends on, and the kernel threads are ~100% busy.
    - SPU busy time by workload (x71 baseline): General6 17%, audio 16%, EDGE 14% each, RenderJobs (13) 8%,
      RenJobSet (11) 4%, BatchJobList (7) 4%. The next lever is SPU compute (General6 first), not the kernel.
    - 2 EDGE lanes instead of 3 (x70): 8.71 / 8.87 against 9.30 / 9.37 game frames/s (EDGE wait 15 against
      7 ms per frame). 3 stays the default.
    - Noise: stretches of a few minutes run ~15-20% slower on every thread at once (EDGE ms/job 0.122
      against 0.108, walker drain 125-128 against 85-98 ms), whatever the settings, 40 minutes to 2.5 hours
      apart (x70, x71, x73, x74; e.g. the 30 s heartbeats of x73w_2 drop from ~9.3 to 7.5-8.4 game frames/s
      between 840 and 1050 s only). Likely a host task; a run that overlaps one is flagged from its heartbeats
      and compared over the clean window, not discarded.
  - **What the HighestPrio6 list waits on** (x71 baseline trace, x72 host samples per workload with
    `PS3_SAMPLE_WIDS`). Per frame, the last span of RenJobSet wid 11 ends a median 0.9 ms before HighestPrio6
    starts and it does not run again until the next frame; General6 (wid 3) ends 2 ms before but keeps running
    through it, so it is not the gate. The RenJobSet lists run as a chain at the end of the frame: wid 9
    (ends ~29 ms before HighestPrio6), wid 10 (~22), wid 11 (26.6 ms wall, 17.2 ms busy) and wid 12 (12.6 ms
    busy in 9 long jobs, ends ~4 ms before).
    - Wid 11 runs as 9 visits of ~2.06 ms (the rerun window) with ~0.86 ms between them, 95% of them on a
      different kernel thread: ~7.4 ms a frame waiting to be picked again, while the SPUs stay full (42.7 of
      44.2 SPU-ms busy in those gaps: General6, EDGE, audio).
    - `SPURS_PM_RERUN_WIDS="11,12=10000"` (toolkit, default unset; x73, alternating, two runs each): wid 11's
      chain 21.0 / 21.0 ms against 27.5 / 25.9, but it starts later by about as much. HighestPrio6 wait 15 / 15
      against 14 / 14 ms per frame, game frames/s 9.23 / 8.74 against 9.22 / 9.30, EDGE wait 9 against 7 ms.
      The chain itself is gated by earlier work, not by its pickups.
    - Host samples while each workload runs: General6 is 61% `jobmod_a67e` `4AB0` and 15% `63B4` (one
      routine; native-stage candidate, ~13% of SPU time). RenJobSet is flat: no function above 6%, ~29% in
      the runtime (`spu_drain_call` 10%, `spu_indirect_branch` 4%, MFC and channels).
    - With the kernel threads full and the end-of-frame lists chained, scheduling knobs are done. What is left
      is SPU compute: General6's `4AB0` as a native stage, and the runtime's dispatch cost on every workload.
- **M0.70: the sixth native EDGE stage, +2.7%/+3.8% (2026-10-25).**
  - The loop nest of EDGE job function `6AC8` (`loc_6BE0`/`loc_6DE0`, ~3.7% of job time: a smooth falloff
    around up to three spheres per group of 8 gathered vertices, accumulated and stored by index) as AVX2
    (`port/gow_edge_native_6ac8.c`). Bit-exact: cap_3 replay 400/400, and an in-game check of 1355776 calls
    against the lifted loop nest with 0 differences, recompute and v1 paths included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first five stages: +2.7% and +3.8% game
    frames/s. Default on; `GOW_EDGE_6AC8=0` restores the lifted loop nest. DCC0 (4.4%) was probed and not
    staged: a serial integer decode with nothing to pack.
- **M0.69: the fifth native EDGE stage, +2-3% (2026-10-25).**
  - The loop of EDGE job function `F538` (`loc_F620`, ~5% of job time: a 4x4 matrix transform of each input
    quad, mostly in place) as hand-written AVX2, two quads per vector (`port/gow_edge_native_f538.c`).
    Bit-exact: cap_3 replay 400/400, and an in-game check of 2392064 calls against the lifted loop with 0
    differences, recompute and fallback paths included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first four stages: +2.3% and +3.3% game
    frames/s (4.10 -> 4.21). Default on; `GOW_EDGE_F538=0` restores the lifted loop.
- **M0.68: the fourth native EDGE stage, +5-6% (2026-10-25).**
  - The loop of EDGE job function `A880` (`loc_A950`, ~7% of job time: per element normalize(B x A * B.w),
    plausibly a bitangent) as hand-written AVX2, 8 elements per vector (`port/gow_edge_native_a880.c`).
    Bit-exact: cap_3 replay 400/400, and an in-game check of 1736704 calls against the lifted loop with 0
    differences, fallbacks included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first three stages: +5.6% and +5.0% game
    frames/s (3.98 -> 4.19). Default on; `GOW_EDGE_A880=0` restores the lifted loop.
- **M0.67: the third native EDGE stage, +3-4% (2026-10-25).**
  - The vertex transform loop of EDGE job function `D250` (`loc_D4DC`) and the triangle culling loop of its
    caller `D640` (`loc_D92C`, index compaction), ~10% of job time together, as hand-written AVX2, two
    iterations per vector (`port/gow_edge_native_d640.c`). Bit-exact: cap_3 replay 400/400, and an in-game
    check of 917504 runs of each loop against the lifted loops with 0 differences (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B (two instances), same exe, on top of the first two stages: +2.9% and +4.2% game
    frames/s (3.88 -> 4.02). Measured alone (one instance, profiled), the cost per EDGE job fell 8.4%, more than
    the replay's -6.7%: the paired A/B understates the gain (docs/EDGE_NATIVE.md). Default on;
    `GOW_EDGE_D640=0` restores the lifted loops.
- **M0.66: the second native EDGE stage, +8% (2026-10-25).**
  - The light-accumulation region of EDGE job function `73E0` (`loc_79B0`..`81C0`, 76% of 73E0, which is
    ~19% of job time) as hand-written AVX2, 8 vertices per vector (`port/gow_edge_native_73e0.c`, v3).
    Bit-exact: cap_3 replay 400/400, and an in-game check of 1146880 region runs against the lifted region
    with 0 differences, fallbacks included (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B, same exe, on top of the E550 stage: +7.8% and +7.9% game frames/s (3.60 -> 3.88).
    Default on; `GOW_EDGE_73E0=0` restores the lifted region.
- **M0.65: the first native EDGE stage, +8-9% (2026-10-25).**
  - The skinning loop of EDGE job function `E550` (`loc_EC08`, 75% of E550, which is ~15% of job time) runs as
    hand-written AVX2 (`port/gow_edge_native.c`, v3): two vertices per 256-bit vector, SPU float rules checked
    once per iteration, with an exact recompute when a lane fails. Bit-exact: cap_3 replay 400/400, and an
    in-game check of 2252800 loop runs against the lifted loop with 0 differences (docs/EDGE_NATIVE.md).
  - Swapped in-game A/B, same exe: +8.0% and +8.8% game frames/s. Default on; `GOW_EDGE_NATIVE=0` restores the
    lifted loop.
  - The loop body the native code reuses is game code: it is generated from the lift at build time, not
    committed.
- **Parallel EDGE lanes (beyond hardware) -- assessed, not started (2026-10-24).**
  - The game registers every workload, EDGE included, with contention min 1 max 1 (now printed at registration):
    on the console each EDGE workload runs on one SPU at a time. Our `SPURS_MAXCONT` lanes run one after another in
    one kernel thread, so they add no parallelism.
  - EDGE priority is `8 8 8 8 0 0 0 3` on a 6-SPU instance: at most 4 lanes (SPUs 0-3), with real spuNums, because
    the WWS sync rows sit at `+0x40+16*spuNum`.
  - Blocker: EDGE job code updates the shared allocator line `0x83257500` with GETLLAR -> compute -> unconditional
    PUTLLUC (pcs `0x424C`/`0x6988`/`0xC760` -> `0x448C`/`0x6724`/`0xC910`). Two lanes would lose updates.
  - Plan, if this is pursued:
    1. The runtime holds a line's reservation from a context's GETLLAR until its next PUTLL*.
    2. One wid, two lanes: a per-wid run count, a distinct spuNum and a persistent PM context per lane, and more
       kernel threads.
    3. Correctness first: no `[pm-hang]`, no texture garbage, every EDGE list completes.
  - The soak's present rate is ~7.1 presents/s (flips ~3.7/s, which is what "game frames/s" counts here).
  - **Re-assessed 2026-10-25: the blocker above was our runtime, not the game.** The allocator line's EA is
    `0x83257480 + 0x80 + 0x80*spuNum` (spuNum from kernel LS `0x1C8`); `0x83257500` is spuNum 0's line, and every
    EDGE run here got spuNum 0 (`spu_run_policy_module(..., 0)` on the default single-lane path). A guest peek
    (x59, `GOW_PEEK` on the five lines) shows the game initialized the lines for spuNum 1-3 with their own
    disjoint rings and labels (main rings `0x83517880`, `0x837D7880`, `0x83A97880`, labels `0xC90`/`0xCA0`/`0xCB0`,
    second rings `0x819C6300`/`0x81A26300`/`0x81A86300`), untouched during play. Lanes that run with distinct
    spuNums use separate lines and rings: no lost updates, and each ring keeps one producer. The `SPURS_MAXCONT`
    path already gives lanes 1..N-1 spuNums 1..N-1; its measured slowness was a host thread per lane per dispatch
    plus `SPURS_PM_RERUN_US` being skipped when maxcont > 1. Job outputs are found by the offsets each job writes
    into its own command segment, so completion order between lanes is free (read-only study of the d935 lift
    and cap_3). Still open: spuNum must be unique across all concurrent EDGE contexts, not only within one wid;
    and whether the GETLLAR/PUTLLC reservation is safe across host threads (its "real lock-line semantics" code
    predates the 2026-07-23 LBP note about two threads racing on a jobIndex CAS).
  - **First lanes runs (toolkit `SPURS_LANES`, 2026-10-25).** Up to n kernel threads may claim one workload at
    once (`s_wid_busy` is a count for those wids), each running the module as the SPU its thread plays, so each
    lane has its own spuNum, PM context and allocator line; no thread is created per dispatch and
    `SPURS_PM_RERUN_US` keeps working per lane. x61 (`15,16,17=2`) froze at ~150 s: the thread playing SPU 5 ran an
    EDGE lane as spuNum 5, whose line the game never set up (EDGE priority is 0 there), and spun at the
    ring-space wait `0x409C`. Lanes now run only on SPUs the workload has a priority on (0-3 for EDGE). x62 (same,
    1100 s, one instance, no PM profiling): no `[pm-hang]`/HALT, the cursors of lines 0, 1 and 2 all move
    (~16.5k changes each), `[edge-diag]` another EDGE wid busy 15-16%, frame dumps clean. t 600-1080 s: 9.08
    game frames/s and 13.96 presents/s, against x56's 5.95 and 11.85 (x56 had PM profiling on; one run each).
    The alternated single-instance measurement follows.
  - Same method on ucrtbase (3.4% of EDGE samples): 97% was `getenv`'s environment walk. The sampler now unwinds such
    samples to their exe caller; all of them came from `spu_run_with_halt`, which read `SPU_ARGWATCH` after every
    module run. It is read once now. The A/B was neutral within noise (runZg vs runZe: -3.2% / +0.4%, EDGE ms/job
    equal), since that cost sits outside the measured job time. Kept: it is a plain per-run cost.
  - What remains on EDGE outside lifted code:
    - the dispatch of the policy module's own functions through its call table (`0x1BA4`/`0x1BE8`/`0x1CC0` from
      `0x2E8C`, ~5k/s each per workload);
    - the generated stubs;
    - `spu_drain_call`.
- `level_rate.py` now tolerates heartbeats that drift off the 30 s grid.
- A fifth pair (x13) also ran clean: 0 of 5 pairs hung with the fix. `VK_SLOW_MS` now also counts live
  `VkDeviceMemory` objects. They peak at ~1094, which is the 1024-entry texture cache full plus targets and rings.
  Slow `vkQueueSubmit` calls happened with as few as 17 alive, so the allocation count alone does not explain
  them. What is still slow adds up to about 6% of a run: image frees from cache evictions (251 calls, 28.6 s),
  submits (67, 21.6 s) and fence waits.
- Upstream: the fix is PR #552 (ported to `master`, which has no batching). PR #532 (`VK_DRAW_BATCH`) failed CI on
  shutdown leaks under the validation layer. A reviewer sent the fix, which is now on the PR branch with their
  authorship. The same leaks (batching rings and the depth-snapshot bounce buffer) are fixed in this fork's toolkit.
- Process Lasso's ProBalance on the test machine can lower the game's priority mid-run. Benchmark with it closed.

**Open bug: texture garbage in the level.** Some meshes and backgrounds sample tiled, multicoloured noise, which
looks like texels read from memory that does not hold the texture (yet). Examples: `vos_1` f_08400 with batching
off, `ubn_2` f_09600-f_11400, and before the M0.51/M0.52 toolkit changes `rge_1`/`rge_2` f_06600 and `rg_1` f_07200
(the worst case, which also hits the HUD). So it is older than this work and varies run to run. A cheap detector over
the dumps: the share of pixels whose colour differs from the pixel 8 to the right with a large chroma change
(> 90). That share is about 0.002-0.006 in clean level frames and 0.012-0.05 in garbage ones; menu frames give
false positives. The noise follows the mesh UVs and comes in blocks: the texture went through the normal
upload path with the wrong guest bytes.
- Ruled out:
  - NV3089 blits sent to the swizzled NV309E destination by the size guess. The title only ever binds context
    surface `0x313371C3` (SURFACE2D, linear), and routing by that handle never disagreed with the guess in 2 runs.
  - Textures that sample an old render target by offset with a compressed format or a larger size than the
    surface: 0 hits in 2 runs (`ENG_ALIAS_DBG`), both of which had garbage.
- Rate: in runs that reach f_09000, garbage started by then in 1 of 19 runs on the M0.51 builds and in 6 of 22 from
  the M0.52 build on. That includes a batching-off run as slow as the old ones. The decrementer change (TSC
  instead of QPC) is the suspected amplifier, under test (`SPU_HOST_NS=qpc`).
The port turns it on (`main.cpp`); `VK_DRAW_BATCH=0` goes back to a submission per draw. Next in the same timeline:
depth snapshots (527 x 4.8 ms) and texture uploads (2992 x 0.35 ms) still submit and wait on their own.

## Ruled out
SPURS HLE vs the real `libsre`; pool base `0x11000000` vs `0x20000000`.

Full chronological log (M0.1 to M0.13): [FEASIBILITY.pt-BR.md](FEASIBILITY.pt-BR.md).
