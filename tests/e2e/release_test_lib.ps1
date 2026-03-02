#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ROOT_DIR = (Resolve-Path "$PSScriptRoot\..\..").Path
$script:RUNTIME_ROOT = Join-Path $ROOT_DIR 'tests\e2e\runtime'
$script:FIXTURE_INDEX_DIR = Join-Path $ROOT_DIR 'tests\fixtures\xim-pkgindex'

function Log($msg) {
    Write-Host "[release-e2e] $msg"
}

function Fail($msg) {
    Write-Error "[release-e2e] FAIL: $msg"
    exit 1
}

function Get-MinimalSystemPath {
    return "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
}

function Require-ReleaseArchive($path) {
    if (-not $path) { $path = Join-Path $ROOT_DIR 'build\release.zip' }
    if (-not (Test-Path $path)) { Fail "release archive not found: $path" }
    return $path
}

function Expand-ReleaseArchive($path, $name) {
    $extractRoot = Join-Path $RUNTIME_ROOT $name
    if (Test-Path $extractRoot) { Remove-Item -Recurse -Force $extractRoot }
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    Expand-Archive -Path $path -DestinationPath $extractRoot -Force

    $pkgDir = Get-ChildItem -Directory "$extractRoot\xlings-*-windows-x86_64" | Select-Object -First 1
    if (-not $pkgDir) { Fail "extracted release package dir not found under $extractRoot" }
    return $pkgDir.FullName
}

function Get-DefaultD2xVersion {
    return '0.1.1'
}

function Write-FixtureReleaseConfig($pkgDir) {
    $config = @{
        version     = '0.4.0'
        mirror      = 'GLOBAL'
        activeSubos = 'default'
        subos       = @{ default = @{ dir = '' } }
        index_repos = @(
            @{ name = 'official'; url = $FIXTURE_INDEX_DIR }
        )
    }
    $config | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $pkgDir '.xlings.json') -Encoding UTF8
}

function Require-FixtureIndex {
    & "$ROOT_DIR\tests\e2e\prepare_fixture_index.sh" $FIXTURE_INDEX_DIR 2>$null
    if (-not (Test-Path "$FIXTURE_INDEX_DIR\pkgs")) {
        Fail "fixture index repo missing at $FIXTURE_INDEX_DIR"
    }
}
