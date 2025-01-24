@echo off

set XLINGS_DIR=C:\Users\Public\xlings
set XLINGS_RUN_DIR=%cd%
set XLINGS_CACHE_DIR=%XLINGS_RUN_DIR%\.xlings

if "%1"=="checker" (
    if not exist "%XLINGS_RUN_DIR%\config.xlings" (
        echo.
        echo **command not supported | config.xlings not found in the current folder**
        echo.
        exit /b 1
    ) else (
        if not exist "%XLINGS_CACHE_DIR%" mkdir "%XLINGS_CACHE_DIR%"
        copy /y "%XLINGS_RUN_DIR%\config.xlings" "%XLINGS_CACHE_DIR%\config-xlings.lua" >nul
        copy /y "%XLINGS_DIR%\tools\template.win.xlings" "%XLINGS_CACHE_DIR%\xmake.lua" >nul
        cd /d "%XLINGS_CACHE_DIR%"

        REM check config file syntax, errorlevel 1 is greater than 1, not errorlevel 1 is less than 1 
        xmake l "%XLINGS_CACHE_DIR%\config-xlings.lua" | findstr /i "error"
        if errorlevel 1 exit /b %errorlevel%
    )
) else (
    cd /d "%XLINGS_DIR%\core"
)

:: xlings command
xmake xlings "%XLINGS_RUN_DIR%" %*
cd /d "%XLINGS_RUN_DIR%"