@echo off

set XLINGS_DIR=C:\Users\Public\xlings
set XLINGS_RUN_DIR=%cd%
set XLINGS_CACHE_DIR=%XLINGS_RUN_DIR%\.xlings

set arg1=%1
set arg2=%2

set "commands=help -h uninstall update drepo"

if "%arg1%"=="" (
    cd %XLINGS_DIR%/core
    xmake xlings $XLINGS_RUN_DIR $arg1 $arg2
    cd %XLINGS_RUN_DIR%
    exit /b
)

echo %commands% | findstr /r "\<%arg1%\>" >nul
if not errorlevel 1 (
    cd %XLINGS_DIR%/core
    xmake xlings %XLINGS_RUN_DIR% %arg1% %arg2%
) else if not exist "%XLINGS_RUN_DIR%/config.xlings" (
    echo     "command not support | not found config.xlings in current folder"
    cd %XLINGS_DIR%/core
    xmake xlings %XLINGS_RUN_DIR%
) else (
    if not exist "%XLINGS_CACHE_DIR%" mkdir "%XLINGS_CACHE_DIR%"
    copy /y "%XLINGS_RUN_DIR%\config.xlings" "%XLINGS_CACHE_DIR%\config.xlings.lua"
    copy /y "%XLINGS_DIR%\tools\template.win.xlings" "%XLINGS_CACHE_DIR%\xmake.lua"

    cd %XLINGS_CACHE_DIR%

    xmake xlings %XLINGS_RUN_DIR% %arg1% %arg2%
)

cd %XLINGS_RUN_DIR%