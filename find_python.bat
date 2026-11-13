@echo off
rem Sets PY to a working Python (used by play.bat and build_core.bat)
set PY=
for %%V in (314 313 312 311 310) do (
  if not defined PY if exist "%LOCALAPPDATA%\Programs\Python\Python%%V\python.exe" set PY="%LOCALAPPDATA%\Programs\Python\Python%%V\python.exe"
)
for %%V in (314 313 312 311 310) do (
  if not defined PY if exist "%ProgramFiles%\Python%%V\python.exe" set PY="%ProgramFiles%\Python%%V\python.exe"
)
if not defined PY if exist "%LOCALAPPDATA%\Programs\Python\Launcher\py.exe" set PY="%LOCALAPPDATA%\Programs\Python\Launcher\py.exe" -3
if not defined PY py -3 --version >nul 2>nul && set PY=py -3
if not defined PY python --version >nul 2>nul && set PY=python
