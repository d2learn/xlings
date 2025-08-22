@echo off

set XLINGS_HOME_DIR=C:\Users\Public\xlings
set XLINGS_DIR=C:\Users\Public\xlings\.xlings
set XLINGS_LATEST_VERSION=C:\Users\Public\xlings\.xlings_latest
set XLINGS_RUN_DIR=%cd%

cd /d "%XLINGS_DIR%\core"

:: xlings command
xmake xlings "%XLINGS_RUN_DIR%" %*

if exist "%XLINGS_LATEST_VERSION%" (

    cd /d "%XLINGS_HOME_DIR%"

    if exist "%XLINGS_DIR%\" (
        rmdir /s /q "%XLINGS_DIR%"
    )

    :: move dir (copy + del)
    robocopy "%XLINGS_LATEST_VERSION%" "%XLINGS_DIR%" /E /MOVE >nul

    if exist "%XLINGS_DIR%\core\" (
        cd /d "%XLINGS_DIR%\core"
    ) else (
        echo Error: Failed to update, Please reinstall xlings...
        echo.
        echo Run Powershell Cmd: [ irm https://d2learn.org/xlings-install.ps1.txt ^| iex ]
        echo.
        exit /b 1
    )

    xmake xlings "%XLINGS_RUN_DIR%" self init
)

cd /d "%XLINGS_RUN_DIR%"
