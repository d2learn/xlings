# One-line installer for xlings (Windows).
#   powershell -ExecutionPolicy Bypass -c "irm https://raw.githubusercontent.com/openxlings/xlings/main/tools/other/quick_install.ps1 | iex"
#Requires -Version 5

param(
    [string]$Version = $env:XLINGS_VERSION
)

$ErrorActionPreference = "Stop"

# --------------- helpers ---------------

function Log-Info  { param([string]$Msg) Write-Host "[xlings]: $Msg" -ForegroundColor Green }
function Log-Warn  { param([string]$Msg) Write-Host "[xlings]: $Msg" -ForegroundColor Yellow }
function Log-Error { param([string]$Msg) Write-Host "[xlings]: $Msg" -ForegroundColor Red }

$GITHUB_REPO = "openxlings/xlings"
$GITHUB_MIRROR = $env:XLINGS_GITHUB_MIRROR

# --------------- banner ---------------

Write-Host @"

 __   __  _      _
 \ \ / / | |    (_)
  \ V /  | |     _  _ __    __ _  ___
   > <   | |    | || '_ \  / _  |/ __|
  / . \  | |____| || | | || (_| |\__ \
 /_/ \_\ |______|_||_| |_| \__, ||___/
                            __/ |
                           |___/

repo:  https://github.com/openxlings/xlings
forum: https://forum.d2learn.org

"@ -ForegroundColor Cyan

# --------------- detect architecture ---------------

$arch = if ([System.Environment]::Is64BitOperatingSystem) { "x86_64" } else { "x86" }
# ARM64 detection (Windows 11+)
if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64" -or $env:PROCESSOR_ARCHITEW6432 -eq "ARM64") {
    $arch = "arm64"
}

# --------------- resolve download URL ---------------

function Resolve-LatestVersion {
    $baseUrl = if ($GITHUB_MIRROR) { $GITHUB_MIRROR } else { "https://github.com" }
    $url = "$baseUrl/$GITHUB_REPO/releases/latest"
    $response = Invoke-WebRequest -Uri $url -MaximumRedirection 10 -UseBasicParsing -ErrorAction Stop
    $finalUrl = $response.BaseResponse.ResponseUri
    if (-not $finalUrl) {
        $finalUrl = $response.BaseResponse.RequestMessage.RequestUri  # PS 7+
    }
    return ($finalUrl.ToString() -split '/')[-1]
}

if ($Version) {
    $latestVersion = $Version
    Log-Info "Using specified version: $latestVersion"
} else {
    Log-Info "Querying latest release..."
    try {
        $latestVersion = Resolve-LatestVersion
    } catch {
        Log-Error "Failed to query the latest release: $_"
        exit 1
    }
}

if (-not $latestVersion) {
    Log-Error "Failed to resolve release version."
    exit 1
}

if ($latestVersion -notmatch '^v') { $latestVersion = "v$latestVersion" }
$versionNum = $latestVersion -replace '^v', ''
$zipName = "xlings-$versionNum-windows-$arch.zip"

if ($GITHUB_MIRROR) {
    $downloadUrl = "$GITHUB_MIRROR/$GITHUB_REPO/releases/download/$latestVersion/$zipName"
} else {
    $downloadUrl = "https://github.com/$GITHUB_REPO/releases/download/$latestVersion/$zipName"
}

Log-Info "Latest version: $latestVersion"
Log-Info "Package:        $zipName"
Log-Info "Download URL:   $downloadUrl"

# --------------- download & extract ---------------

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "xlings-install-$([System.Guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

$zipPath = Join-Path $tempDir $zipName

try {
    Log-Info "Downloading..."
    $progressPref = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
    $ProgressPreference = $progressPref

    Log-Info "Extracting..."
    Expand-Archive -Path $zipPath -DestinationPath $tempDir -Force

    $extractDir = Get-ChildItem -Path $tempDir -Directory -Filter "xlings-*" | Select-Object -First 1
    $xlingsBin = if ($extractDir) { Join-Path $extractDir.FullName "bin\xlings.exe" } else { $null }
    if (-not $extractDir -or -not (Test-Path $xlingsBin)) {
        Log-Error "Extracted package is invalid (missing bin\xlings.exe)."
        exit 1
    }

    Log-Info "Running installer..."
    Push-Location $extractDir.FullName
    & $xlingsBin self install
    Pop-Location
} catch {
    Log-Error "Installation failed: $_"
    exit 1
} finally {
    Log-Info "Cleaning up temporary files..."
    Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
}
