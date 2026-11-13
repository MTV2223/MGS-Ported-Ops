@echo off
cd /d "%~dp0"
call "%~dp0find_python.bat"
if not defined PY (
  echo Could not find Python on this PC.
  pause
  exit /b
)
%PY% make_packs.py
