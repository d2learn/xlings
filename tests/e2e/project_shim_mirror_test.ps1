#Requires -Version 5.1
# E2E test: verify that project-context `xlings install` mirrors shims to
# global subos bin so that tools are reachable via PATH.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\release_test_lib.ps1"

Require-FixtureIndex

$SCENARIO_DIR = Join-Path $ROOT_DIR 'tests\e2e\scenarios\local_repo'
$HOME_DIR     = Join-Path $RUNTIME_ROOT 'shim_mirror_home'
$GLOBAL_BIN   = Join-Path $HOME_DIR 'subos\default\bin'
$PROJECT_BIN  = Join-Path $SCENARIO_DIR '.xlings\subos\_\bin'

# --- Find xlings binary ---
$XLINGS_BIN = Get-ChildItem "$ROOT_DIR\build\*\*\release\xlings.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $XLINGS_BIN) { Fail "xlings.exe binary not found under build\" }
$XLINGS_BIN = $XLINGS_BIN.FullName

# --- Backup scenario config ---
$BACKUP_FILE = [System.IO.Path]::GetTempFileName()
Copy-Item "$SCENARIO_DIR\.xlings.json" $BACKUP_FILE

function Cleanup {
    if (Test-Path "$SCENARIO_DIR\.xlings") { Remove-Item -Recurse -Force "$SCENARIO_DIR\.xlings" }
    if (Test-Path $HOME_DIR) { Remove-Item -Recurse -Force $HOME_DIR }
    if (Test-Path $BACKUP_FILE) {
        Copy-Item $BACKUP_FILE "$SCENARIO_DIR\.xlings.json"
        Remove-Item $BACKUP_FILE
    }
}

trap { Cleanup }

# --- Setup isolated XLINGS_HOME ---
if (Test-Path $HOME_DIR) { Remove-Item -Recurse -Force $HOME_DIR }
if (Test-Path "$SCENARIO_DIR\.xlings") { Remove-Item -Recurse -Force "$SCENARIO_DIR\.xlings" }
New-Item -ItemType Directory -Force -Path $HOME_DIR | Out-Null

# Copy xlings binary
Copy-Item $XLINGS_BIN (Join-Path $HOME_DIR 'xlings.exe')

# Write home config
$homeConfig = @{
    mirror      = 'GLOBAL'
    index_repos = @(
        @{ name = 'xim'; url = $FIXTURE_INDEX_DIR }
    )
}
$homeConfig | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $HOME_DIR '.xlings.json') -Encoding UTF8

# --- Clean global subos bin ---
if (Test-Path $GLOBAL_BIN) { Remove-Item -Recurse -Force $GLOBAL_BIN }
New-Item -ItemType Directory -Force -Path $GLOBAL_BIN | Out-Null

# --- Pre-check ---
if (Test-Path "$GLOBAL_BIN\node.exe") { Fail "global node shim already exists before test" }

# --- Run update + install in project context ---
$env:XLINGS_HOME = $HOME_DIR
Push-Location $SCENARIO_DIR
try {
    & $XLINGS_BIN update
    if ($LASTEXITCODE -ne 0) { Fail "xlings update failed" }

    & $XLINGS_BIN -y install
    if ($LASTEXITCODE -ne 0) { Fail "xlings install failed" }
} finally {
    Pop-Location
    Remove-Item Env:XLINGS_HOME -ErrorAction SilentlyContinue
}

# --- Verify: project subos bin has the shim ---
if (-not (Test-Path "$PROJECT_BIN\node.exe")) { Fail "project node shim missing after install" }

# --- Verify: global subos bin now also has the shim ---
if (-not (Test-Path "$GLOBAL_BIN\node.exe")) { Fail "global node shim was NOT mirrored after project install" }

# --- Verify: npm/npx bindings also mirrored ---
foreach ($binding in @('npm.exe', 'npx.exe')) {
    if (-not (Test-Path "$GLOBAL_BIN\$binding")) {
        Fail "global $binding binding shim was NOT mirrored"
    }
}

# --- Verify: pre-existing global shim is not overwritten ---
Set-Content "$GLOBAL_BIN\node.exe" "marker"
$env:XLINGS_HOME = $HOME_DIR
Push-Location $SCENARIO_DIR
try {
    & $XLINGS_BIN -y install 2>&1 | Out-Null
} finally {
    Pop-Location
    Remove-Item Env:XLINGS_HOME -ErrorAction SilentlyContinue
}
$marker = Get-Content "$GLOBAL_BIN\node.exe" -Raw
if ($marker.Trim() -ne "marker") { Fail "global node shim was overwritten (should preserve existing)" }

Cleanup
Log "PASS: project shim mirror to global subos bin"
