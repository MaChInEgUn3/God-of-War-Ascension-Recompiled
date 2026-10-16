@echo off
rem Sets up MSVC (for the Windows SDK) + the portable clang-cl and runs the given command.
rem Required: GOW_ROOT = the project checkout. Optional: VCVARS overrides the vcvars64.bat path.
if "%GOW_ROOT%"=="" (echo set GOW_ROOT first & exit /b 1)
if "%VCVARS%"=="" set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>&1
set "PATH=%GOW_ROOT%\local\tools\llvm\bin;%GOW_ROOT%\local\tools\venv\Scripts;%PATH%"
cd /d "%GOW_ROOT%\port"
%*
