# Install xlings from a self-contained release package (Windows).
# Run this script from inside the extracted release directory:
#   Expand-Archive xlings-<ver>-windows-x86_64.zip -DestinationPath .
#   cd xlings-<ver>-windows-x86_64
#   powershell -ExecutionPolicy Bypass -File install.ps1

$ErrorActionPreference = "Stop"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SCRIPT_DIR

# Sanity check
if (-not (Test-Path "bin") -or -not (Test-Path "subos") -or -not (Test-Path "xim")) {
    Write-Error "[xlings]: This does not look like a valid xlings release package."
    Write-Error "Expected bin\, subos\, xim\ directories in: $SCRIPT_DIR"
    exit 1
}

$DEFAULT_XLINGS_HOME = "$env:USERPROFILE\.xlings"

function Detect-ExistingXlingsHome {
    $existingCmd = Get-Command xlings -ErrorAction SilentlyContinue
    if (-not $existingCmd) { return $null }
    $binDir = Split-Path -Parent $existingCmd.Source
    $candidate = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $binDir))
    if ((Test-Path "$candidate\bin") -and (Test-Path "$candidate\subos")) {
        return $candidate
    }
    return $null
}

$OLD_XLINGS_HOME = $null
if ($env:XLINGS_HOME -and $env:XLINGS_HOME -ne $DEFAULT_XLINGS_HOME) {
    $OLD_XLINGS_HOME = $env:XLINGS_HOME
}

if (-not $OLD_XLINGS_HOME) {
    $OLD_XLINGS_HOME = Detect-ExistingXlingsHome
}

if ($OLD_XLINGS_HOME -and $OLD_XLINGS_HOME -ne $DEFAULT_XLINGS_HOME) {
    Write-Host "[xlings]: Detected existing xlings at: $OLD_XLINGS_HOME" -ForegroundColor Yellow
    Write-Host "[xlings]: Default install directory is: $DEFAULT_XLINGS_HOME" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  [1] Overwrite existing installation at $OLD_XLINGS_HOME" -ForegroundColor Cyan
    Write-Host "  [2] Install to default location $DEFAULT_XLINGS_HOME (keep old)" -ForegroundColor Cyan
    Write-Host ""
    $choice = Read-Host "Choose [1/2] (default: 1)"
    if ($choice -eq "2") {
        $XLINGS_HOME = $DEFAULT_XLINGS_HOME
    } else {
        $XLINGS_HOME = $OLD_XLINGS_HOME
    }
} else {
    $XLINGS_HOME = $DEFAULT_XLINGS_HOME
}

Write-Host "[xlings]: Installing xlings to $XLINGS_HOME" -ForegroundColor Green

# Create XLINGS_HOME if it doesn't exist
if (-not (Test-Path $XLINGS_HOME)) {
    New-Item -ItemType Directory -Force -Path $XLINGS_HOME | Out-Null
}

# Copy package contents to XLINGS_HOME
if ($SCRIPT_DIR -ne (Resolve-Path $XLINGS_HOME -ErrorAction SilentlyContinue)) {
    Write-Host "[xlings]: Copying package to $XLINGS_HOME ..." -ForegroundColor Green
    Get-ChildItem -Path $SCRIPT_DIR -Force | ForEach-Object {
        $dest = Join-Path $XLINGS_HOME $_.Name
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        Copy-Item -Recurse -Force $_.FullName $dest
    }
} else {
    Write-Host "[xlings]: Already running from $XLINGS_HOME, skipping copy." -ForegroundColor Green
}

# Recreate subos\current junction
$currentLink = "$XLINGS_HOME\subos\current"
$defaultDir = (Resolve-Path "$XLINGS_HOME\subos\default").Path
if (Test-Path $currentLink) { Remove-Item -Force -Recurse $currentLink }
New-Item -ItemType Junction -Path $currentLink -Target $defaultDir | Out-Null

$XLINGS_BIN = "$XLINGS_HOME\subos\current\bin"

# Add to user PATH
$userPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$XLINGS_BIN*") {
    $newPath = "$XLINGS_BIN;$userPath"
    [System.Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "[xlings]: Added $XLINGS_BIN to user PATH" -ForegroundColor Green
} else {
    Write-Host "[xlings]: PATH already contains $XLINGS_BIN" -ForegroundColor Green
}

# Update PATH for current session
$env:Path = "$XLINGS_BIN;$XLINGS_HOME\bin;$env:Path"

# Verify
try {
    $helpOut = & "$XLINGS_HOME\bin\xlings.exe" -h 2>&1 | Out-String
    if ($helpOut -match "xlings") {
        Write-Host "[xlings]: Verification passed." -ForegroundColor Green
    }
} catch {
    Write-Host "[xlings]: Warning: xlings binary test failed." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[xlings]: xlings installed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "    Run 'xlings -h' to get started." -ForegroundColor Yellow
Write-Host "    Restart your terminal to refresh PATH." -ForegroundColor Cyan
Write-Host ""
