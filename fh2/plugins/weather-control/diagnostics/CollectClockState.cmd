@echo off
cd /d "%~dp0"
if exist "C:\Users\romul\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" (
  "C:\Users\romul\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 "%~dp0CollectClockState.py" %*
) else (
  py -3 "%~dp0CollectClockState.py" %*
)
pause
