@echo off

REM SCRIPT_DIR end with \
set "SCRIPT_DIR=%~dp0"
set "RUN_DIR=%cd%"
set XLINGS_PROJECT_DIR=%SCRIPT_DIR%..
set XLINGS_TMP_BIN_DIR=%XLINGS_PROJECT_DIR%\bin
set XMAKE_BIN_DIR=%USERPROFILE%\xmake
set XLINGS_BIN_DIR=C:\Users\Public\xlings\.xlings_data\bin

set arg1=%1

echo [RunDir]: %RUN_DIR%
echo [ProjectDir]: %XLINGS_PROJECT_DIR%

cd %XLINGS_PROJECT_DIR%

set "PATH=%XLINGS_TMP_BIN_DIR%;%PATH%"

echo [xlings]: start detect environment and try to auto config...

REM Check if xmake is installed
where xmake >nul 2>&1

IF %ERRORLEVEL% EQU 0 (
    echo [xlings]: xmake installed
) else (
    REM xmake is not installed, downloading and running install script using PowerShell
    echo [xlings]: start install xmake...
    powershell -Command "irm https://xmake.io/psget.text | iex"
)

REM install git
where git >nul 2>&1

IF %ERRORLEVEL% EQU 0 (
    echo [xlings]: git installed
) else (
    echo [xlings]: start install git...
    winget install git.git --accept-source-agreements
)

REM 2. set xlings to PATH
for /f "tokens=2*" %%a in ('reg query "HKEY_CURRENT_USER\Environment" /v PATH') do set UserPath=%%b
echo %UserPath% | findstr /i "xlings_data" >nul
if %errorlevel% neq 0 (
    echo [xlings]: set xlings to PATH
    setx PATH "%XLINGS_BIN_DIR%;%UserPath%"
) else (
    echo [xlings]: xlings is already in PATH.
)

if exist "%cd%/install.win.bat" (
    cd ..
)

set "PATH=%XLINGS_BIN_DIR%;%XMAKE_BIN_DIR%;%PATH%"

REM 3. install xlings
cd core
xmake xlings --project=. unused self enforce-install
cd ..

REM 4. install xvm
REM 5. check if xlings command exists
where xlings >nul 2>&1
if %errorlevel% neq 0 (
    echo [xlings]: xlings command not found, installation failed
    exit /b 1
)

REM 6. config xlings
xlings self init

REM 6. install info
echo [xlings]: xlings installed
echo.
echo     run xlings help get more information
echo.

REM 5. update env
REM if "%arg1%"=="disable_reopen" (
REM    echo [xlings]: cmd - disable reopen
REM ) else (
REM    REM update env by start cmd : TODO disable version info /Q?
REM    cmd
REM )