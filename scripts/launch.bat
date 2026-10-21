@echo off
rem Starts the player launcher (launcher\gow_launcher.py). Needs Python 3 with Tk (the python.org installer has it).
rem pythonw keeps a console window from staying open; fall back to python if it is not on PATH.
where pythonw >nul 2>&1
if %errorlevel%==0 (
  start "" pythonw "%~dp0..\launcher\gow_launcher.py"
) else (
  python "%~dp0..\launcher\gow_launcher.py"
)
