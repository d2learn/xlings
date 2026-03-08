#Requires -Version 5.1
# E2E test: verify that packages from sub-index repos (discovered via
# xim-indexrepos.lua in the main repo) are visible to 'xlings search'.
# Also tests pkgindex-build.lua support (template appending via os.files C++ impl).
# This is the Windows counterpart of sub_index_search_test.sh.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ROOT_DIR = (Resolve-Path "$PSScriptRoot\..\..").Path
$FIXTURE_INDEX_DIR = Join-Path $ROOT_DIR 'tests\fixtures\xim-pkgindex'
$RUNTIME_DIR = Join-Path $ROOT_DIR 'tests\e2e\runtime\sub_index_search_home'
$BUILD_DIR = Join-Path $ROOT_DIR 'build'

function Log($msg) { Write-Host "[sub-index-search-e2e] $msg" }
function Fail($msg) { Write-Error "[sub-index-search-e2e] FAIL: $msg"; exit 1 }

function Find-XlingsBin {
    if ($env:XLINGS_BIN -and (Test-Path $env:XLINGS_BIN)) { return $env:XLINGS_BIN }
    $candidate = Get-ChildItem -Recurse $BUILD_DIR -Filter 'xlings.exe' | Where-Object { $_.FullName -match 'release' } | Select-Object -First 1
    if (-not $candidate) { Fail "xlings.exe not found; set XLINGS_BIN" }
    return $candidate.FullName
}

$XLINGS_BIN = Find-XlingsBin
Log "Using xlings binary: $XLINGS_BIN"

# Ensure fixture index exists
if (-not (Test-Path "$FIXTURE_INDEX_DIR\pkgs")) { Fail "fixture index repo not found at $FIXTURE_INDEX_DIR" }

# Ensure git identity
git config user.name 2>$null || git config --global user.name "xlings-ci"
git config user.email 2>$null || git config --global user.email "ci@xlings.test"

# ── Cleanup ──
$SUB_INDEX_DIR = Join-Path $ROOT_DIR 'tests\fixtures\xim-pkgindex-testd2x-win'
$BUILD_INDEX_DIR = Join-Path $ROOT_DIR 'tests\fixtures\xim-pkgindex-testbuild-win'
$INDEXREPOS_LUA = Join-Path $FIXTURE_INDEX_DIR 'xim-indexrepos.lua'

function Cleanup {
    if (Test-Path $RUNTIME_DIR) { Remove-Item -Recurse -Force $RUNTIME_DIR }
    if (Test-Path $SUB_INDEX_DIR) { Remove-Item -Recurse -Force $SUB_INDEX_DIR }
    if (Test-Path $BUILD_INDEX_DIR) { Remove-Item -Recurse -Force $BUILD_INDEX_DIR }
    if (Test-Path $INDEXREPOS_LUA) { Remove-Item -Force $INDEXREPOS_LUA }
}

Cleanup  # start fresh

# ── 1. Create sub-index fixture with metadata-only package ──
New-Item -ItemType Directory -Force -Path "$SUB_INDEX_DIR\pkgs\t" | Out-Null
@'
package = {
    name = "testpkg-d2x",
    description = "Test package from sub-index repo",
    authors = "test",
    license = "MIT",
    repo = "https://example.com/test",
}
'@ | Set-Content "$SUB_INDEX_DIR\pkgs\t\testpkg-d2x.lua" -Encoding UTF8

Push-Location $SUB_INDEX_DIR
git init -q; git add -A; git commit -q -m "init"
Pop-Location

# ── 2. Create sub-index with pkgindex-build.lua (template appending via os.files) ──
New-Item -ItemType Directory -Force -Path "$BUILD_INDEX_DIR\pkgs\b" | Out-Null
@'
package = {
    name = "buildpkg",
    description = "Test package requiring pkgindex-build",
    authors = "test",
    license = "MIT",
    repo = "https://example.com/buildpkg",
}
'@ | Set-Content "$BUILD_INDEX_DIR\pkgs\b\buildpkg.lua" -Encoding UTF8

@'

package.type = "courses"
package.xpm = {
    linux = { ["latest"] = { url = package.repo .. ".git" } },
    macosx = { ["latest"] = { url = package.repo .. ".git" } },
    windows = { ["latest"] = { url = package.repo .. ".git" } },
}
'@ | Set-Content "$BUILD_INDEX_DIR\template.lua" -Encoding UTF8

@'
package = {
    name = "pkgindex-update",
    description = "Test pkgindex build script",
    xpm = { linux = { ["latest"] = {} } },
}
local projectdir = os.scriptdir()
local pkgsdir = path.join(projectdir, "pkgs")
local template = path.join(projectdir, "template.lua")
function installed() return false end
function install()
    local files = os.files(path.join(pkgsdir, "**.lua"))
    local template_content = io.readfile(template)
    for _, file in ipairs(files) do
        if not file:endswith("pkgindex-update.lua") then
            io.writefile(file, io.readfile(file) .. template_content)
        end
    end
    return true
end
function uninstall() return true end
'@ | Set-Content "$BUILD_INDEX_DIR\pkgindex-build.lua" -Encoding UTF8

Push-Location $BUILD_INDEX_DIR
git init -q; git add -A; git commit -q -m "init"
Pop-Location

# ── 3. Write xim-indexrepos.lua ──
# Use forward slashes in file:// URLs for git compatibility on Windows
$subUrl = "file:///$($SUB_INDEX_DIR -replace '\\','/')"
$buildUrl = "file:///$($BUILD_INDEX_DIR -replace '\\','/')"
@"
xim_indexrepos = {
    ["testd2x"] = {
        ["GLOBAL"] = "$subUrl",
    },
    ["testbuild"] = {
        ["GLOBAL"] = "$buildUrl",
    }
}
"@ | Set-Content $INDEXREPOS_LUA -Encoding UTF8

# ── 4. Set up XLINGS_HOME ──
New-Item -ItemType Directory -Force -Path $RUNTIME_DIR | Out-Null
New-Item -ItemType Directory -Force -Path "$RUNTIME_DIR\subos\default\bin" | Out-Null
@"
{
  "mirror": "GLOBAL",
  "index_repos": [
    {
      "name": "xim",
      "url": "$($FIXTURE_INDEX_DIR -replace '\\','/')"
    }
  ]
}
"@ | Set-Content "$RUNTIME_DIR\.xlings.json" -Encoding UTF8

function Run-Xlings {
    param([string[]]$Args)
    $env:XLINGS_HOME = $RUNTIME_DIR
    & $XLINGS_BIN @Args
}

# ── 5. Run update to sync repos ──
Log "Running xlings update..."
Run-Xlings update

# ── 6. Verify sub-repos were synced ──
$syncedD2x = Join-Path $RUNTIME_DIR 'data\xim-index-repos\xim-pkgindex-testd2x-win'
if (-not (Test-Path "$syncedD2x\pkgs")) { Fail "sub-index repo testd2x was not synced" }

$syncedBuild = Join-Path $RUNTIME_DIR 'data\xim-index-repos\xim-pkgindex-testbuild-win'
if (-not (Test-Path "$syncedBuild\pkgs")) { Fail "sub-index repo testbuild was not synced" }

# ── 7. Verify xim-indexrepos.json ──
$jsonFile = Join-Path $RUNTIME_DIR 'data\xim-index-repos\xim-indexrepos.json'
if (-not (Test-Path $jsonFile)) { Fail "xim-indexrepos.json was not created" }
$jsonContent = Get-Content $jsonFile -Raw
if ($jsonContent -notmatch 'testd2x') { Fail "testd2x not in xim-indexrepos.json" }
if ($jsonContent -notmatch 'testbuild') { Fail "testbuild not in xim-indexrepos.json" }

# ── 8. Search metadata-only package ──
Log "Running xlings search testpkg-d2x..."
$searchOut = (Run-Xlings search testpkg-d2x 2>&1) | Out-String
Write-Host $searchOut
if ($searchOut -notmatch 'testpkg-d2x') { Fail "search did not find testpkg-d2x" }
if ($searchOut -notmatch 'testd2x') { Fail "search result missing testd2x namespace" }

# ── 9. Search for pkgindex-build package (validates os.files C++ impl) ──
Log "Running xlings search buildpkg..."
$searchBuildOut = (Run-Xlings search buildpkg 2>&1) | Out-String
Write-Host $searchBuildOut
if ($searchBuildOut -notmatch 'testbuild:buildpkg') { Fail "search did not find testbuild:buildpkg" }

# ── 10. Verify built package has xpm versions (info shows 'latest') ──
Log "Running xlings info testbuild:buildpkg..."
$infoOut = (Run-Xlings info testbuild:buildpkg 2>&1) | Out-String
Write-Host $infoOut
if ($infoOut -notmatch 'latest') { Fail "info for testbuild:buildpkg should show 'latest' version" }

# ── Cleanup ──
Cleanup

Log "PASS: sub-index repo search + pkgindex-build (Windows)"
