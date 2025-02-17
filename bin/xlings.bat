@echo off

set XLINGS_DIR=C:\Users\Public\xlings
set XLINGS_RUN_DIR=%cd%
set XLINGS_CACHE_DIR=%XLINGS_RUN_DIR%\.xlings

if exist "%XLINGS_RUN_DIR%\config.xlings" (
    if not exist "%XLINGS_CACHE_DIR%" mkdir "%XLINGS_CACHE_DIR%"
    copy /y "%XLINGS_RUN_DIR%\config.xlings" "%XLINGS_CACHE_DIR%\config-xlings.lua" >nul
    copy /y "%XLINGS_DIR%\tools\template.win.xlings" "%XLINGS_CACHE_DIR%\xmake.lua" >nul
)

set "NEED_LOAD_PROJECT_FILE="
if "%1"=="d2x" set "NEED_LOAD_PROJECT_FILE=1"
if "%1"=="install" if "%2"=="" if exist "%XLINGS_RUN_DIR%\config.xlings" set "NEED_LOAD_PROJECT_FILE=1"

if defined IS_PROJECT (
    cd /d "%XLINGS_CACHE_DIR%"
    REM check config file syntax, errorlevel 1 is greater than 1, not errorlevel 1 is less than 1 
    xmake l "%XLINGS_CACHE_DIR%\config-xlings.lua" | findstr /i "error"
    if errorlevel 1 exit /b %errorlevel%
) else (
    cd /d "%XLINGS_DIR%\core"
)

:: xlings command
xmake xlings "%XLINGS_RUN_DIR%" %*
cd /d "%XLINGS_RUN_DIR%"