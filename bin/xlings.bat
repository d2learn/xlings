@echo off

set XLINGS_DIR=C:\Users\Public\xlings
set XLINGS_RUN_DIR=%cd%
set XLINGS_CACHE_DIR=%XLINGS_RUN_DIR%\.xlings

cd /d "%XLINGS_DIR%\core"

:: xlings command
xmake xlings "%XLINGS_RUN_DIR%" %*
cd /d "%XLINGS_RUN_DIR%"