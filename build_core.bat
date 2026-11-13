@echo off
rem Builds psp_core.dll (the fast CPU + graphics) from psp_core.c and psp_gpu.c.
rem Downloads the Zig C compiler through pip the first time (about 50 MB, no admin rights needed).
cd /d "%~dp0"
call "%~dp0find_python.bat"
if not defined PY ( echo Could not find Python on this PC. & pause & exit /b )
echo Installing the C compiler (ziglang) if needed...
%PY% -m pip install --user ziglang
echo Building psp_core.dll ...
%PY% -m ziglang cc -O2 -shared -o psp_core.dll psp_core.c psp_gpu.c
if exist psp_core.dll (echo. & echo DONE: psp_core.dll was built.) else (echo. & echo The build did not work. Copy the text above and send it to Claude.)
pause
