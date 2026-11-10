@echo off
rem Optional profile-guided build (PGO) of the lifted PPU, the lifted SPU and the runtime together.
rem Needs GOW_ROOT like cfg.bat. Steps and measured gain: docs\BUILDING.md, "Optional: profile-guided build".
rem   pgo.bat gen    configure + build port\build_pgogen: instrumented exe (-fprofile-generate, GOW_PGO_GEN)
rem   pgo.bat merge  %GOW_ROOT%\local\pgo\gow.profraw -> gow.profdata
rem   pgo.bat use    configure + build port\build_pgouse, optimized with gow.profdata
rem The configure line is cfg.bat's plus the profile flags: keep the two in step.
rem CLANG_RT_DIR overrides where clang_rt.profile-x86_64.lib is (the "23" is the clang major version).
if "%GOW_ROOT%"=="" (echo set GOW_ROOT first & exit /b 1)
if "%CLANG_RT_DIR%"=="" set "CLANG_RT_DIR=%GOW_ROOT%/local/tools/llvm/lib/clang/23/lib/windows"
set "PGO_DATA=%GOW_ROOT%/local/pgo/gow.profdata"
if "%1"=="gen" goto gen
if "%1"=="merge" goto merge
if "%1"=="use" goto use
echo usage: pgo.bat gen^|merge^|use
exit /b 2

:merge
call "%GOW_ROOT%\scripts\env.bat" llvm-profdata merge -o "%PGO_DATA%" "%GOW_ROOT%/local/pgo/gow.profraw"
exit /b %ERRORLEVEL%

:gen
set BDIR=build_pgogen
set PGOFLAGS=/clang:-fprofile-generate /DGOW_PGO_GEN
set PGOLINK=/LIBPATH:\"%CLANG_RT_DIR%\" clang_rt.profile-x86_64.lib
goto build

:use
if not exist "%PGO_DATA%" goto noprofile
set BDIR=build_pgouse
set PGOFLAGS=/clang:-fprofile-use=\"%PGO_DATA%\" -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin
set PGOLINK=
goto build

:noprofile
echo no profile at %PGO_DATA%: run the gen build, play it with GOW_PGO_DUMP set, then "pgo.bat merge"
exit /b 1

:build
call "%GOW_ROOT%\scripts\env.bat" cmake -B %BDIR% -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DPS3RECOMP_DIR="%GOW_ROOT%/local/tools/ps3recomp" -DRECOMP_DIR="%GOW_ROOT%/local/recompiled" -DCMAKE_EXE_LINKER_FLAGS="dxguid.lib /MAP %PGOLINK%" -DPS3RECOMP_RSX_VULKAN=ON -DCMAKE_C_FLAGS="-I\"%GOW_ROOT%/local/tools/sdl2/inc\" %PGOFLAGS%" -DCMAKE_CXX_FLAGS="-I\"%GOW_ROOT%/local/tools/sdl2/inc\" %PGOFLAGS%" -DPS3RECOMP_VULKAN_INCLUDE_DIR="%GOW_ROOT%/local/tools/vulkan-headers/include" -DCMAKE_PREFIX_PATH="%GOW_ROOT%/local/tools/glslang/sdk/Library" -DSDL2_DIR="%GOW_ROOT%/local/tools/sdl2/SDL2-2.32.10/cmake" -DPS3RECOMP_FFMPEG_RUNTIME=ON -DPS3RECOMP_FFMPEG_INCLUDE_DIR="%GOW_ROOT%/local/tools/ffmpeg/sdk/include"
if errorlevel 1 exit /b 1
call "%GOW_ROOT%\scripts\env.bat" ninja -C %BDIR% MyGameRecomp
exit /b %ERRORLEVEL%
