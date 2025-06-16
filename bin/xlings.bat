@echo off

set XLINGS_DIR=C:\Users\xlings\.xlings
set XLINGS_RUN_DIR=%cd%

cd /d "%XLINGS_DIR%\core"

:: xlings command
xmake xlings "%XLINGS_RUN_DIR%" %*
cd /d "%XLINGS_RUN_DIR%"