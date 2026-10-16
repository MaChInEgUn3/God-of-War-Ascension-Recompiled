# God of War: Ascension — static recompilation (PS3)

> **Status: research / early bring-up (private).** The game boots through its CRT, FIOS, 19 PSARC archives, the
> SPURS/WWS job system and Ice::Render GCM initialisation, then stalls before the first draw call.
> Nothing is playable yet. See [docs/STATUS.md](docs/STATUS.md) for the current blocker.

An attempt to statically recompile *God of War: Ascension* (PS3, `BCUS98232`) into a native Windows executable,
built on top of the [ps3recomp](https://github.com/sp00nznet/ps3recomp) toolkit (PPU lifter, SPU lifter,
HLE runtime, Vulkan/D3D12 RSX backends).

**This repository contains no game data, no firmware and no code derived from them.** You need your own legally
acquired copy of the game and (optionally) your own PS3 firmware dump. See [docs/LEGAL.md](docs/LEGAL.md).

## What lives here

| Path | Contents |
|------|----------|
| `port/` | The game-specific port: `CMakeLists.txt`, `main.cpp` (runner), `stubs.cpp` (hooks), `lle_libsre.cpp` (optional firmware-SPURS LLE glue), `config.toml` |
| `patches/` | Patch against upstream ps3recomp (fixes and diagnostics found while bringing the game up) — meant to be upstreamed |
| `spu/images.json` | Description of the SPU images lifted for this title (policy module, SPURS kernel, ...) |
| `analysis/` | Python tools used to measure the binary (carving SPU blobs, PSARC TOC, xrefs, `findstore.py`, `make_spu_port.py`) |
| `scripts/` | Windows build helpers (`GOW_ROOT`-relative) |
| `docs/` | Feasibility study (pt-BR), architecture notes, debugging playbook, related work, legal |
| `local/` | **Git-ignored.** Your game dump, decrypted ELF, firmware, lifted C++, toolchains, logs |

## Quick start

See [docs/BUILDING.md](docs/BUILDING.md). In short: dump your disc, decrypt `EBOOT.BIN` to an ELF, run the
ps3recomp loader/lifter on it, apply `patches/`, configure with `scripts/cfg.bat`, build with `scripts/build.bat`,
run `MyGameRecomp.exe <path>/PS3_GAME/USRDIR/EBOOT.elf`.

## Related projects

See [docs/RELATED_PROJECTS.md](docs/RELATED_PROJECTS.md): ps3recomp and its game ports, RPCS3 (used as an oracle),
XenonRecomp / UnleashedRecomp, N64: Recompiled, and others.

## License

MIT for the code in this repository (see [LICENSE](LICENSE)). Upstream ps3recomp is MIT as well. Game assets,
firmware and anything derived from them are **not** covered and must never be committed.
