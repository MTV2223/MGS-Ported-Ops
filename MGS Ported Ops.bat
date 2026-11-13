@echo off
cd /d "%~dp0"
call "%~dp0find_python.bat"
if not defined PY (
  echo Could not find Python on this PC. Install Python 3.10 or newer from python.org, then try again.
  echo.
  echo Press any key to close this window.
  pause >nul
  exit /b
)
%PY% -c "import PIL" >nul 2>nul || (
  echo Installing Pillow, needed for the launcher and the game - one time only...
  %PY% -m pip install --user pillow
)
%PY% launcher.py
if errorlevel 1 pause
