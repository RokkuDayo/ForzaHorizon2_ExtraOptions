@echo off
setlocal
where py >nul 2>nul
if errorlevel 1 goto use_python
py -3 -X utf8 "%~dp0mod_diff.py" %*
set "diff_exit=%errorlevel%"
goto finished
:use_python
where python >nul 2>nul
if errorlevel 1 goto missing
python -X utf8 "%~dp0mod_diff.py" %*
set "diff_exit=%errorlevel%"
goto finished
:missing
set "diff_exit=1"
echo Python 3.10 or newer is required. Install Python and enable its launcher or PATH option.
:finished
echo.
pause
exit /b %diff_exit%
