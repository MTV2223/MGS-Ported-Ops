@echo off
cd /d "%~dp0"
call "%~dp0find_python.bat"
if not defined PY (
  echo Could not find Python on this PC.
  echo Open a Python window and paste this line instead:
  echo exec^(open^(r"%~dp0play.py"^).read^(^)^)
  echo.
  echo Press any key to close this window.
  pause >nul
  exit /b
)
%PY% -c "import PIL" >nul 2>nul || (
  echo Installing Pillow, needed for the game's save / message screens - one time only...
  %PY% -m pip install --user pillow
)
echo Starting the game with: %PY%
%PY% play.py
echo.
echo The game has closed. Press any key to close this window.
pause >nul
