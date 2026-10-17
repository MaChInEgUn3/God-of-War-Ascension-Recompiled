# Building (Windows)

Prerequisites: Windows 10/11, Visual Studio (Windows SDK + vcvars), CMake >= 3.20, Ninja, Python 3.12 (+ `capstone`),
portable LLVM/clang-cl (MSVC `cl` is **not** supported by ps3recomp), SDL2 dev package, Vulkan headers. Keep everything
local under `local/` (git-ignored) and set `GOW_ROOT` to the checkout.

1. Clone this repo, and upstream `sp00nznet/ps3recomp` (commit `a2c2010` or newer) into `local/tools/ps3recomp`; apply
   `patches/ps3recomp-gow-m0.patch` with `git apply`.
2. Dump your disc and decrypt `EBOOT.BIN` to an ELF (for example `rpcs3 --decrypt`). Put the game tree in
   `local/vfs/PS3_GAME/...`; the archives listed in `ARCHIVES` must be present (`LOCALIZED_ARCHIVES` lists the language ones).
3. Load and lift: `ppu_loader`, then `ppu_lifter.py <elf> -o local/recompiled --functions ... --hle-stubs ...`
   (about 50 s, about 190 MB of C++). Regenerate the NID table:
   `gen_hle_nids.py --all --out local/recompiled/ppu_hle_nids.cpp`.
4. SPU images: `python analysis/make_spu_port.py <elf> spu/images.json port <ps3recomp>` (generates `port/spu/`, git-ignored).
   WWS job modules (33, embedded in the EBOOT):
   `python analysis/extract_gow_jobmods.py <elf> local/spu/jobmods`, then
   `JOBMOD_EXTRA_FUNCS_JSON=spu/jobmod_extra.json python <ps3recomp>/tools/lift_jobmods.py --jobmods local/spu/jobmods
   --out port/spu --register port/spu/jobmods_register.c --symbol-prefix gow` (image ids 200-232, matched at run
   time by the 16-byte module header). `spu/jobmod_extra.json` lists entries reached only indirectly; refresh it
   with `SPU_INTERP_MISS=1` runs and `analysis/interp_miss_to_extra.py` (see DEBUGGING.md).
5. `scripts\cfg.bat`, then `scripts\build.bat` (first build about 7 min and about 8 GB RAM; incremental builds take
   seconds unless the lifted header changes).
6. Run `port\build_vk\MyGameRecomp.exe local\vfs\PS3_GAME\USRDIR\EBOOT.elf` (needs `SDL2.dll` next to the exe;
   set `PS3RECOMP_VK_WINDOW=1` to open the window).

## Optional: firmware SPURS (LLE)

Decrypt your own `libsre.sprx`, then run `lift_prx.py libsre.prx --base 0x30000000`,
`ppu_lifter.py ... --raw --base 0x30000000 --toc 0x30039AB0 --code-end 0x3001D718 --symbol-prefix libsre_ --hle-stubs ...`
and `prx_exports.py`. Output goes to `local/lle/`. Run with `GOW_LLE=real`.
Details: ps3recomp `docs/FIRMWARE_LLE.md`.
