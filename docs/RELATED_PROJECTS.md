# Related projects

**PS3**
* [sp00nznet/ps3recomp](https://github.com/sp00nznet/ps3recomp): the toolkit this port is built on (PPU/SPU lifters, HLE runtime, D3D12/Vulkan RSX backends). Game ports by the same author are listed in the repository and in press coverage (Rubber Ducky, Twisted Metal 2012, Saints Row 2, PlayStation All-Stars Battle Royale).
* [RPCS3](https://github.com/RPCS3/rpcs3): the reference emulator (LLVM PPU recompiler, ASMJIT SPU); used here as a behavioural oracle.
* Coverage: [PS3Recomp article (Generation Amiga)](https://www.generationamiga.com/2026/06/15/ps3recomp-the-new-tool-bringing-native-playstation-3-ports-closer-to-pc/).

**Other static recompilation projects (structure and policy worth copying)**
* [hedge-dev/XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and XenosRecomp (Xbox 360 PPC and shaders), [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp): recompiler *tool* separated from the *game port*; TOML config; function-hook mechanism; users supply the game files.
* [N64Recomp](https://github.com/N64Recomp/N64Recomp), N64ModernRuntime and N64: Recompiled: tool / runtime / per-game project split.
* [LibertyRecomp](https://github.com/UnknownLITE/LibertyRecomp): states plainly that no game assets are included.

**Practices adopted here**: tool / runtime / port separation, config-driven inputs, hooks instead of editing generated
code, no game data or lifted output in the repository, a documented legal stance, a reproducible local build and
oracle comparison against RPCS3.
