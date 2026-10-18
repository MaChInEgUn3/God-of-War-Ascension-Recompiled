@echo off
rem Run the recompiled game in a window (Vulkan). Needs GOW_ROOT and a build in %GOW_ROOT%\local\port\build_vk.
rem The window is opt-in (PS3RECOMP_VK_WINDOW=1); without it the backend renders headless.
cd /d "%GOW_ROOT%"
set PS3RECOMP_VK_WINDOW=1
set PS3RECOMP_VK_GUEST_PROGRAMS=1
set PPU_TRACE_ADDRS=0
set PPU_TRACE_MAX=0
"%GOW_ROOT%\local\port\build_vk\MyGameRecomp.exe" "%GOW_ROOT%/local/vfs/PS3_GAME/USRDIR/EBOOT.elf" > "%GOW_ROOT%\local\play.log" 2>&1
