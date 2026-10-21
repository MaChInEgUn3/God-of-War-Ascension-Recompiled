# Launcher

`gow_launcher.py` is a small Tk window for playing a local build. Python 3 standard library only.

```
scripts\launch.bat            (or: python launcher\gow_launcher.py)
python -m unittest launcher/test_launcher.py -v
```

You need your own build (`MyGameRecomp.exe` with its DLLs, see [docs/BUILDING.md](../docs/BUILDING.md)) and your
own dump of the game (`<game folder>/PS3_GAME/USRDIR/EBOOT.elf` plus the PSARC archives). The launcher ships none of it.

## What it does

* **Paths**: the exe, `EBOOT.elf`, and optionally the save-data root (`PS3_GAMEDATA_ROOT`, default
  `<exe folder>/gamedata`) and the cache root (`PS3_HDD1_ROOT`, default `<game folder>/cache`). Saved in
  `launcher/settings.json` (git-ignored, plain JSON you can edit).
* **Check setup**: exe and `EBOOT.elf` exist, every archive in `USRDIR/ARCHIVES` is present, which language
  archives are present, `SDL2.dll` / SPIRV-Tools / FFmpeg DLLs next to the exe. It never runs the exe.
* **Play / Stop**: runs the exe from its own folder with `EBOOT.elf` as the argument. Output goes to
  `launcher/logs/gow_<date>_<time>.log` (the last N are kept), or to its own console window. Stop ends only the
  process this launcher started. Closing the launcher leaves the game running.
* **Saves**: "Back up saves" zips `<save root>/dev_hdd0/home/00000001/savedata/BCUS98232*` into
  `launcher/backups/`. "Restore" backs up the current saves first, then extracts only `BCUS98232*` entries.

## Options and the variables they set

Hover a label to see the variable. The extra-environment box (`KEY=VALUE` per line) is applied last and wins.

| Option | Variable | Read by |
|---|---|---|
| Show window (on) | `PS3RECOMP_VK_WINDOW=1` | ps3recomp `libs/video/rsx_vulkan_backend.c` (`vk_window_open`) |
| Full screen | `PS3RECOMP_VK_FULLSCREEN=1` | same, borderless desktop full screen |
| Game shaders (on) | `PS3RECOMP_VK_GUEST_PROGRAMS=1` | same; off draws black frames |
| GPU index | `PS3RECOMP_VK_DEVICE=<n>` | same (device selection) |
| Game language | `PS3_LANG=<code>` | `libs/system/cellSysutil.c`; blank = Windows language |
| Skip intro movie | `PAD_SCRIPT=40:0x4000` | `libs/input/cellPad.c`: CROSS at 40 s (DEBUGGING.md, "Faster runs") |
| Release guard (on) | `GOW_RELEASE_GUARD=0` when off | `port/gow_vram_guard.c`, a stability mitigation |
| Save-data root | `PS3_GAMEDATA_ROOT` | `libs/system/cellSaveData.c` |
| Cache root | `PS3_HDD1_ROOT` | `runtime/ppu/ppu_fs.cpp`, `runtime/syscalls/sys_fs.c` |

It also sets `PPU_TRACE_ADDRS=0` / `PPU_TRACE_MAX=0`, like `scripts/play.bat`. The Vulkan backend has no window-size,
resolution or vsync variable (the swapchain uses FIFO, i.e. vsync), so the launcher offers none.
