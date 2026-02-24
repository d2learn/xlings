Param(
  [string]$ArchivePath = ""
)

$ErrorActionPreference = "Stop"

function Log($msg) { Write-Host "[e2e-windows] $msg" }
function Fail($msg) { throw "[e2e-windows] FAIL: $msg" }

function Assert-Contains {
  Param(
    [string]$Output,
    [string]$Expected,
    [string]$Hint = ""
  )
  if ($Output -notmatch [regex]::Escape($Expected)) {
    Fail "expected output to contain '$Expected'. $Hint"
  }
}

$RootDir = (Resolve-Path "$PSScriptRoot\..\..").Path
$BuildDir = Join-Path $RootDir "build"
$TempBase = Join-Path ([System.IO.Path]::GetTempPath()) ("xlings-e2e-win-" + [guid]::NewGuid().ToString("N"))
$SkipNetworkTests = if ($env:SKIP_NETWORK_TESTS) { $env:SKIP_NETWORK_TESTS } else { "1" }
$D2XVersion = if ($env:D2X_VERSION) { $env:D2X_VERSION } else { "0.1.2" }

function Prepare-Runtime {
  if ($env:XLINGS_HOME -and (Test-Path $env:XLINGS_HOME)) {
    return $env:XLINGS_HOME
  }

  if (-not $ArchivePath) {
    $zip = Get-ChildItem "$BuildDir\xlings-*-windows-x86_64.zip" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $zip) { Fail "windows release archive not found" }
    $ArchivePath = $zip.FullName
  }

  if (-not (Test-Path $ArchivePath)) {
    Fail "archive not found: $ArchivePath"
  }

  New-Item -ItemType Directory -Path $TempBase -Force | Out-Null
  Expand-Archive -Path $ArchivePath -DestinationPath $TempBase -Force
  $pkgDir = Get-ChildItem -Directory "$TempBase\xlings-*-windows-x86_64" | Select-Object -First 1
  if (-not $pkgDir) { Fail "failed to locate extracted package dir" }
  return $pkgDir.FullName
}

function Setup-Env([string]$PkgDir) {
  $env:XLINGS_HOME = $PkgDir
  $env:XLINGS_DATA = "$PkgDir\data"
  $env:XLINGS_SUBOS = "$PkgDir\subos\current"
  $env:PATH = "$PkgDir\subos\current\bin;$PkgDir\bin;$env:PATH"
}

function Scenario-BasicCommands {
  Log "scenario: basic commands"
  $helpOut = (xlings -h 2>&1) | Out-String
  Assert-Contains $helpOut "Commands:"
  Assert-Contains $helpOut "info"

  $cfgOut = (xlings config 2>&1) | Out-String
  Assert-Contains $cfgOut "XLINGS_HOME"
  Assert-Contains $cfgOut "XLINGS_SUBOS"

  $verOut = (xvm --version 2>&1) | Out-String
  Assert-Contains $verOut "xvm"
}

function Scenario-InfoMapping {
  Log "scenario: info mapping"
  $infoOut = (xlings info xlings 2>&1) | Out-String
  Assert-Contains $infoOut "Program: xlings"
}

function Scenario-SubosLifecycleAndAliases {
  Log "scenario: subos lifecycle and aliases"
  $lsOut = (xlings subos ls 2>&1) | Out-String
  Assert-Contains $lsOut "default"

  xlings subos new s1 | Out-Null
  xlings subos use s1 | Out-Null

  $iOut = (xlings subos i s1 2>&1) | Out-String
  Assert-Contains $iOut "info for 's1'"
  Assert-Contains $iOut "active: true"

  xlings subos list | Out-Null
  xlings subos info s1 | Out-Null

  xlings subos use default | Out-Null
  xlings subos rm s1 | Out-Null
}

function Scenario-SelfAndCleanup {
  Log "scenario: self and cleanup"
  $out = (xlings self clean --dry-run 2>&1) | Out-String
  Assert-Contains $out "dry-run"
}

function Scenario-NetworkInstallOptional {
  if ($SkipNetworkTests -eq "1") {
    Log "scenario: network install (skipped, SKIP_NETWORK_TESTS=1)"
    return
  }

  Log "scenario: network install"
  $out = (xlings install "d2x@$D2XVersion" -y 2>&1) | Out-String
  Assert-Contains $out "installed"
}

try {
  $pkgDir = Prepare-Runtime
  Setup-Env $pkgDir

  Log "runtime: $pkgDir"
  Scenario-BasicCommands
  Scenario-InfoMapping
  Scenario-SubosLifecycleAndAliases
  Scenario-SelfAndCleanup
  Scenario-NetworkInstallOptional

  Log "PASS: all usability scenarios passed"
}
finally {
  if (Test-Path $TempBase) {
    Remove-Item -Recurse -Force $TempBase
  }
}

