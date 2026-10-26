# Contributing

Help is welcome: code, measurements, bug reports and tests on hardware we do not have. Pull requests are reviewed
by hand; nothing is merged automatically.

## Ground rules

* Read [docs/LEGAL.md](docs/LEGAL.md) first. Changes containing game data, firmware or lifted output are rejected,
  and so is code copied from projects whose license is not compatible with MIT.
* Changes to the ps3recomp toolkit go in `patches/ps3recomp-gow-m0.patch` (regenerate it against tag `v0.17.0`, see
  [docs/BUILDING.md](docs/BUILDING.md)). Fixes that are not specific to this game should also go upstream to
  [ps3recomp](https://github.com/sp00nznet/ps3recomp) as small PRs.
* Update `docs/STATUS.md` (and `docs/DEBUGGING.md` for a new switch) in the same change as the code. For findings,
  `docs/FEASIBILITY.pt-BR.md` keeps the chronological log.
* New diagnostics and experiments: behind an environment variable, off by default. A new default needs a
  measurement (below).
* Commits: imperative subject, body explains *why*. Prefixes: `port:`, `patch:`, `analysis:`, `docs:`.

## Getting started

You need your own copy of the game (`BCUS98232`). [docs/BUILDING.md](docs/BUILDING.md) goes from a disc dump to a
running build; the first build takes about 7 minutes and 8 GB of RAM. [docs/STATUS.md](docs/STATUS.md) says where
things stand and [docs/DEBUGGING.md](docs/DEBUGGING.md) lists every diagnostic switch.

## Measuring performance

Frame rate varies a lot between runs on the same machine, so a performance claim needs:

* Runs of at least ~18 minutes into the first level, one instance at a time, comparing the window 600-1080 s.
  The `[heartbeat]` lines give game frames and presents; game frames per second is the number that matters.
* Alternating runs (A, B, A, B), at least two of each. Delete the FIOS cache (`vfs/cache/BCUS98232/fios/cache.*`)
  before every run (DEBUGGING.md, "A/B timing hygiene").
* Where the time goes, not only the total: `PS3_FTRACE=600:20:<file>` records a 20 s per-thread timeline, and
  `ftrace_report.py` (toolkit `tools/`) prints per-frame blocked/busy time for the main thread, the SPU kernel
  threads and the RSX walker. `PS3_SAMPLE=2 PS3_SAMPLE_HOST=1` with `analysis/samp_resolve.py` gives a host
  profile (`PS3_SAMPLE_WIDS` narrows it to one SPU workload).

Quote the measured lines in the PR. A claim that was not measured should say so.

## Where help counts most

The current bound and the open leads are kept in [docs/STATUS.md](docs/STATUS.md). At the time of writing:

* **SPU runtime dispatch.** The lifted SPU code spends roughly a fifth of its time in the runtime's indirect-branch,
  lookup and drain path (`spu_indirect_branch`, `spu_lookup`, `spu_drain_call`), on every workload. The SPU kernel
  threads are the bound, so this is the largest lever. It is generic toolkit work.
* **RSX walker readbacks.** Depth and colour targets sampled with another format are copied to guest memory and
  uploaded again as textures, ~19 ms of walker time per frame. A GPU-side reinterpretation would remove most of
  it, as long as the guest copy is still written when the game's own code reads it.
* **Other hardware.** Everything so far was measured on one Windows PC (6-core CPU, NVIDIA GPU). Results on other
  CPUs, AMD/Intel GPUs or Linux, with the heartbeat numbers, help.
* **Bugs.** Rendering errors with a frame dump (`PS3RECOMP_VK_DUMP`), hangs with the log of the run.

## CI guard

`.github/workflows/guard.yml` fails the build if forbidden files (game, firmware or lifted output) or files over
2 MB are tracked.
