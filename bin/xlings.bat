@echo off

set XLINGS_DIR=C:\Users\Public\xlings
set PROJECT_DIR=%cd%
set XLINGS_CACHE_DIR=%PROJECT_DIR%\.xlings

set arg1=%1
set arg2=%2

set "commands=help uninstall update"

if "%arg1%"=="" (
    cd %XLINGS_DIR%/core
    xmake xlings
    exit /b
)

echo %commands% | findstr /r "\<%arg1%\>" >nul
if not errorlevel 1 (
    cd %XLINGS_DIR%/core
    xmake xlings %arg1%
) else if not exist "%PROJECT_DIR%/config.xlings" (
    echo     "command not support | not found config.xlings in current folder"
    cd %XLINGS_DIR%/core
    xmake xlings
) else (
    if not exist "%XLINGS_CACHE_DIR%" mkdir "%XLINGS_CACHE_DIR%"
    copy /y "%PROJECT_DIR%\config.xlings" "%XLINGS_CACHE_DIR%\config.xlings.lua"
    copy /y "%XLINGS_DIR%\tools\template.win.xlings" ""%XLINGS_CACHE_DIR%\xmake.lua"

    cd %XLINGS_CACHE_DIR%

    xmake xlings %arg1% %arg2%
)