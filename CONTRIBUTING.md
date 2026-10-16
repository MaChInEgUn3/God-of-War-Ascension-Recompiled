# Contributing

* Read [docs/LEGAL.md](docs/LEGAL.md) first. Changes containing game data, firmware or lifted output are rejected.
* Keep changes to upstream ps3recomp as patches in `patches/` (and send them upstream once stable).
* Update `docs/STATUS.md` and, for findings, `docs/FEASIBILITY.pt-BR.md` in the same change as the code.
* Prefer diagnostics that are env-gated and off by default.
* Commits: imperative subject, body explains *why*. Prefixes: `port:`, `patch:`, `analysis:`, `docs:`.

## CI guard

`.github/workflows/guard.yml` fails the build if forbidden files (game, firmware or lifted output) or files over 2 MB are tracked.
