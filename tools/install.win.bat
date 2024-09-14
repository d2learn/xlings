@echo off

set XLINGS_BIN_DIR=C:\Users\Public\xlings\bin

echo [xlings]: start detect environment and try to auto config...

REM Check if xmake is installed
where xmake >nul 2>&1

IF %ERRORLEVEL% EQU 0 (
    echo [xlings]: xmake installed
) else (
    REM xmake is not installed, downloading and running install script using PowerShell
    echo [xlings]: start install xmake...
    powershell -Command "Invoke-Expression ((Invoke-WebRequest 'https://xmake.io/psget.text' -UseBasicParsing).Content)"
)

REM 2. set xlings to PATH
for /f "tokens=2*" %%a in ('reg query "HKEY_CURRENT_USER\Environment" /v PATH') do set UserPath=%%b
echo %UserPath% | findstr /i "%XLINGS_BIN_DIR%" >nul
if %errorlevel% neq 0 (
    echo [xlings]: set xlings to PATH
    setx PATH "%UserPath%;%XLINGS_BIN_DIR%"
) else (
    echo [xlings]: xlings is already in PATH.
)

if exist "%cd%/install.win.bat" (
    cd ..
)

REM 3. install xlings
cd core
xmake xlings unused install xlings
cd ..

REM 4. install info
echo [xlings]: xlings installed
echo.
echo     run xlings help get more information (after restart vscode)
echo.