@echo off
rem  build.cmd -- one-click Duke Nukem 3D 64 ROM build.
rem
rem  1. copy the DUKE3D.GRP of your copy of the game into  gamedata\
rem     and each expansion into its own folder, e.g.  gamedata\nwinter\
rem  2. run this file
rem  3. the ROMs appear in  output\  (duke3d.z64, duke3d-nwinter.z64, ...)
rem
rem  Extra options are passed straight through, e.g.:
rem     build.cmd base            only the base game
rem     build.cmd nwinter         only gamedata\nwinter
setlocal
cd /d "%~dp0"

set "PY=python"
where python >nul 2>&1 || set "PY=py -3"

%PY% tools\pack_rom.py %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo Build FAILED ^(exit %RC%^).
) else (
    echo Done. Check output\ for your ROMs.
)
pause
exit /b %RC%
