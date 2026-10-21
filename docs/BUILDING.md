# Building (Windows)

Prerequisites: Windows 10/11, Visual Studio (Windows SDK + vcvars), CMake >= 3.20, Ninja, Python 3.12 (+ `capstone`),
portable LLVM/clang-cl (MSVC `cl` is **not** supported by ps3recomp), SDL2 dev package, Vulkan headers. Keep everything
local under `local/` (git-ignored) and set `GOW_ROOT` to the checkout.

1. Clone this repo, and upstream `sp00nznet/ps3recomp` at tag `v0.17.0` (commit `a6d76c8`) into `local/tools/ps3recomp`;
   apply `patches/ps3recomp-gow-m0.patch` with `git apply`. The patch is regenerated against that tag (it was against
   `749182e`/v0.16.0 until 2026-10-21, and `a2c2010` before M0.40). v0.17.0 already contains our earlier upstream PRs.
   Configure the port with `-DPORT_GENERATE_HLE_NIDS=ON` so the HLE NID table is generated from the toolkit (v0.17
   renamed handlers an older lift's table names); otherwise re-run
   `tools/gen_hle_nids.py --all --out <recompiled>/ppu_hle_nids.cpp` after switching the toolkit.
2. Dump your disc and decrypt `EBOOT.BIN` to an ELF (for example `rpcs3 --decrypt`). Put the game tree in
   `local/vfs/PS3_GAME/...`; the archives listed in `ARCHIVES` must be present (`LOCALIZED_ARCHIVES` lists the language ones).
3. Load and lift: `ppu_loader`, then `ppu_lifter.py <elf> -o local/recompiled --functions ... --hle-stubs ...`
   (about 50 s, about 190 MB of C++). Then `python scripts/patch_lift.py local/recompiled` inserts the port's
   function hooks (today: the VRAM-cache release guard in `port/gow_vram_guard.c`; idempotent). Regenerate the NID table:
   `gen_hle_nids.py --all --out local/recompiled/ppu_hle_nids.cpp`. Rerun it whenever the patch adds an HLE function
   (NIDs are computed from the names; GoW:A needs at least `cellVdecQueryAttrEx`/`cellVdecOpenEx`).
   Optional: real MPEG-2 decoding of the menu video needs FFmpeg 9.0 (`-DPS3RECOMP_FFMPEG_RUNTIME=ON` with its headers,
   every DLL from the FFmpeg `bin` next to the exe -- avcodec alone fails to load on its dependencies); without it
   the video decodes to black. Tested with BtbN `ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip` unpacked to
   `local/tools/ffmpeg/sdk` (`scripts/cfg.bat` points there).
5. Play: `scripts/play.bat` opens a window (`PS3RECOMP_VK_WINDOW=1`; without it the Vulkan backend renders headless).
4. SPU images: `python analysis/make_spu_port.py <elf> spu/images.json port <ps3recomp>` (generates `port/spu/`, git-ignored).
   WWS job modules (33, embedded in the EBOOT):
   `python analysis/extract_gow_jobmods.py <elf> local/spu/jobmods`, then
   `JOBMOD_EXTRA_FUNCS_JSON=spu/jobmod_extra.json python <ps3recomp>/tools/lift_jobmods.py --jobmods local/spu/jobmods
   --out port/spu --register port/spu/jobmods_register.c --symbol-prefix gow --merge-chunks` (image ids 200-232, matched at run
   time by the 16-byte module header). `spu/jobmod_extra.json` lists entries reached only indirectly; refresh it
   with `SPU_INTERP_MISS=1` runs and `analysis/interp_miss_to_extra.py` (see DEBUGGING.md).
   EDGE code fragments (EBOOT blobs the EDGE jobs stage at LS `0x11090+`, one lift per placement): run with
   `SPU_LSA_GETLOG=10000-3E000 SPU_LSA_GETLOG_EA=10000-820000 SPU_INTERP_MISS=1`, then
   `python analysis/lift_edge_frags.py --merge-chunks <elf> port <ps3recomp> <run log>...` (generates `port/spu/edgefrag_*` and
   `port/spu/edgefrags_register.c`, image ids 300+). A placement not seen in the logs stays interpreted. Pass a copy of
   the previous `edgefrags_register.c` among the inputs to keep its placements (the menu and the first level use 360),
   then re-run `scripts\cfg.bat` so CMake picks up the new directories.
   PPU lifts made before ps3recomp's inline guest loads: `python analysis/patch_ppu_inline_loads.py <ps3recomp>
   <recompiled>/ppu_recomp.h` copies the lifter's `vm_read*_inl` block into the header (a full PPU rebuild follows,
   about 45 min). Measured neutral on GoW:A's level; kept so the header matches what the lifter emits. Read
   diagnostics (`PPU_RWATCH`, `PPU_HOTMAP`, `PPU_HOTREAD`, `PPU_FORCE_READ_ADDR`, ...) turn the inline path off.
5. `scripts\cfg.bat`, then `scripts\build.bat` (first build about 7 min and about 8 GB RAM; incremental builds take
   seconds unless the lifted header changes). The link writes `build_vk\MyGameRecomp.map` (`/MAP`), which
   `analysis/samp_resolve.py` uses to name `PS3_SAMPLE_HOST` samples. The lifted SPU sources build with `-O2 -march=x86-64-v3
   -ffp-contract=off` (AVX2 + FMA). On a CPU without AVX2/FMA, configure with `-DSPU_HOST_ARCH=-msse4.2`. Do not
   drop below SSE4.1: the `shufb` and quadword byte-rotate helpers fall back to byte loops, and `fma`/`fms`/`fnms`
   become libm `fmaf` calls; the SPU jobs then run about 2x slower.
6. Run `port\build_vk\MyGameRecomp.exe local\vfs\PS3_GAME\USRDIR\EBOOT.elf` (needs `SDL2.dll` next to the exe;
   set `PS3RECOMP_VK_WINDOW=1` to open the window).

## Guest shaders (Vulkan): glslang

The game's RSX programs are decompiled to HLSL and compiled to SPIR-V by glslang. Without it the Vulkan backend
draws nothing useful (black frames). The Windows SDK's `dxc.exe` is built without SPIR-V codegen, so it does not help.
An MSVC-ABI build that works is conda-forge's `win-64` packages `glslang-16.5.0-h8fa7867_2.conda` and
`spirv-tools-2026.3-h49e36cd_1.conda`, from `https://conda.anaconda.org/conda-forge/win-64/`. Check each SHA-256
against `https://api.anaconda.org/package/conda-forge/<name>/files`.

1. Each `.conda` file is a zip. Unzip it, then extract its `pkg-*.tar.zst` with Windows `tar.exe` into
   `local/tools/glslang/sdk`. The result is `sdk/Library/{lib,bin,include}`.
2. `scripts\cfg.bat` passes `-DCMAKE_PREFIX_PATH=.../glslang/sdk/Library`. The configure log must say
   `Vulkan: guest programs available (glslang 16.5.0)`. The patch adds that `find_package` on Windows.
3. Copy `SPIRV-Tools.dll` and `SPIRV-Tools-opt.dll` from `sdk/Library/bin` next to the exe.
4. Run with `PS3RECOMP_VK_GUEST_PROGRAMS=1`. `PS3RECOMP_VK_DUMP=<file.ppm>` with `PS3RECOMP_VK_DUMP_EVERY=<n>`
   writes every n-th presented frame to that file.

## Optional: firmware SPURS (LLE)

Decrypt your own `libsre.sprx`, then run `lift_prx.py libsre.prx --base 0x30000000`,
`ppu_lifter.py ... --raw --base 0x30000000 --toc 0x30039AB0 --code-end 0x3001D718 --symbol-prefix libsre_ --hle-stubs ...`
and `prx_exports.py`. Output goes to `local/lle/`. Run with `GOW_LLE=real`.
Details: ps3recomp `docs/FIRMWARE_LLE.md`.
