#!/usr/bin/env pwsh
#Requires -version 5

function Show-Progress {
    param (
        [string]$Activity,
        [int]$PercentComplete
    )
    if ($PercentComplete -ge 100) {
        Write-Progress -Activity $Activity -Completed
    } else {
        Write-Progress -Activity $Activity -PercentComplete $PercentComplete
    }
}

$xlingsBinDir = "C:\Users\Public\xlings\bin"
$softwareName = "xlings"
$tempDir = [System.IO.Path]::GetTempPath()
$installDir = Join-Path $tempDir $softwareName
$zipFile = Join-Path $tempDir "$softwareName.zip"
$downloadUrl = "https://github.com/d2learn/xlings/archive/refs/heads/main.zip"

# ---------------------------------------------------------------

# Clear old-cache
if (Test-Path $installDir) {
    Remove-Item $installDir -Recurse -Force
}

if (Test-Path $zipFile) {
    Remove-Item $zipFile -Force
}


# Create Dir
New-Item -ItemType Directory -Force -Path $installDir | Out-Null

# Downloading
Show-Progress -Activity "Downloading $softwareName" -PercentComplete 10
try {
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipFile -UseBasicParsing
}
catch {
    Write-Error "Failed to download the software: $_"
    Read-Host "Press Enter to exit"
    exit 1
}

# Extracting
Show-Progress -Activity "Extracting $softwareName" -PercentComplete 30
try {
    Expand-Archive -Path $zipFile -DestinationPath $installDir -Force
}
catch {
    Write-Error "Failed to extract the software: $_"
    Read-Host "Press Enter to exit"
    exit 1
}

# Install
Show-Progress -Activity "Installing $softwareName" -PercentComplete 50
$installScript = Get-ChildItem -Path $installDir -Recurse -Include "install.win.bat" | Select-Object -First 1
if ($installScript) {
    try {
        Push-Location $installScript.Directory
        cd $installDir\xlings-main
        if ($installScript.Name -eq "install.win.bat") {
            & cmd.exe /c $installScript.FullName disable_reopen
        }
        else {
            #& $installScript.FullName
        }
        Pop-Location
    }
    catch {
        Write-Error "Failed to run the installation script: $_"
        Read-Host "Press Enter to exit"
        exit 1
    }
}
else {
    Write-Error "Installation script not found: $installScript"
    Read-Host "Press Enter to exit"
    exit 1
}

# Clear
Show-Progress -Activity "Cleaning up..." -PercentComplete 70
Write-Host "removing directory: $installDir (tmpfiles)"
Remove-Item $installDir -Recurse -Force
Remove-Item $zipFile -Force

Show-Progress -Activity "Installation complete" -PercentComplete 100
Write-Host "$softwareName has been successfully installed."

# Update env
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","User") + ";" + $xlingsBinDir