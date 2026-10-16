# Contributing

* Read [docs/LEGAL.md](docs/LEGAL.md) first. Changes containing game data, firmware or lifted output are rejected.
* Keep changes to upstream ps3recomp as patches in `patches/` (and send them upstream once stable).
* Update `docs/STATUS.md` and, for findings, `docs/FEASIBILITY.pt-BR.md` in the same change as the code.
* Prefer diagnostics that are env-gated and off by default.
* Commits: imperative subject, body explains *why*. Prefixes: `port:`, `patch:`, `analysis:`, `docs:`.

## CI guard

`docs/ci/guard.yml.example` is a GitHub Actions job that fails if forbidden files (game/firmware/lifted output) or
files over 2 MB are tracked. To enable it: `gh auth refresh -s workflow`, then move it to `.github/workflows/guard.yml`.
