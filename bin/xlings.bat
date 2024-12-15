@echo off

set XLINGS_DIR=C:\Users\Public\xlings
set XLINGS_RUN_DIR=%cd%
set XLINGS_CACHE_DIR=%XLINGS_RUN_DIR%\.xlings

set arg1=%1
set arg2=%2

set "commands=help -h uninstall update drepo config"

if "%arg1%"=="" (
    cd /d %XLINGS_DIR%/core
    xmake xlings %XLINGS_RUN_DIR% %arg1% %arg2% %3 %4 %5
    cd /d %XLINGS_RUN_DIR%
    exit /b
)

if "%arg1%"=="run" (
    if not exist "%XLINGS_CACHE_DIR%" mkdir "%XLINGS_CACHE_DIR%"
    cd /d %XLINGS_DIR%/core
    xmake xlings %XLINGS_RUN_DIR% run %arg2% %3 %4 %5
    cd /d %XLINGS_RUN_DIR%
    exit /b
)

echo %commands% | findstr /r "\<%arg1%\>" >nul
if not errorlevel 1 (
    cd /d %XLINGS_DIR%/core
    xmake xlings %XLINGS_RUN_DIR% %arg1% %arg2% %3 %4 %5
) else if not exist "%XLINGS_RUN_DIR%/config.xlings" (
    cd /d %XLINGS_DIR%/core
    if "%arg1%"=="install" (
        xmake xlings %XLINGS_RUN_DIR% install %arg2% %3 %4 %5
    ) else (
        echo     "command not support | not found config.xlings in current folder"
        xmake xlings %XLINGS_RUN_DIR%
    )
) else (
    if not exist "%XLINGS_CACHE_DIR%" mkdir "%XLINGS_CACHE_DIR%"
    copy /y "%XLINGS_RUN_DIR%\config.xlings" "%XLINGS_CACHE_DIR%\config-xlings.lua" >nul
    copy /y "%XLINGS_DIR%\tools\template.win.xlings" "%XLINGS_CACHE_DIR%\xmake.lua" >nul

    cd /d %XLINGS_CACHE_DIR%

    REM check config file syntax, errorlevel 1 is greater than 1, not errorlevel 1 is less than 1 
    xmake l "%XLINGS_CACHE_DIR%\config-xlings.lua" | findstr /i "error"
    if errorlevel 1 (
        xmake xlings %XLINGS_RUN_DIR% %arg1% %arg2% %3 %4 %5
    )
)

cd /d %XLINGS_RUN_DIR%

exit /b

REM TODO: fix powershell/cmd Path issues

REM Environment variables refreshed - tmp.
REM reg query "HKCU\Environment"

REM System Path
for /f "skip=1 tokens=1,2,*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v Path') do (
    if "%%a"=="Path" set "SYSTEM_PATH=%%c"
)

REM User Path
for /f "skip=1 tokens=1,2,*" %%a in ('reg query "HKCU\Environment" /v Path') do (
    if "%%a"=="Path" set "PATH=%%c"
)

call set "PATH=%SYSTEM_PATH%;%PATH%"