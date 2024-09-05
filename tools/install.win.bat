@echo off
REM Define color codes
set "GREEN=^<ESC^>[92m"
set "YELLOW=^<ESC^>[93m"
set "RESET=^<ESC^>[0m"

REM Enable delayed expansion to use color codes
setlocal enabledelayedexpansion

echo [xlings]: start detect environment and try to auto config...

REM Check if xmake is installed
where xmake >nul 2>&1

IF %ERRORLEVEL% EQU 0 (
    echo !GREEN![xlings]: xmake installed!RESET!
) else (
    REM xmake is not installed, downloading and running install script using PowerShell
    echo [xlings]: start install xmake...
    powershell -Command "Invoke-Expression ((Invoke-WebRequest 'https://xmake.io/psget.text' -UseBasicParsing).Content)"
)

call setup.bat

REM 2. install xlings
cd core
xmake xlings install

REM 3. install info
echo [xlings]: xlings installed
echo.
echo     run !YELLOW!xlings help!RESET! get more information
echo.