# Build a bootstrap xlings package for Windows x86_64.
#
# Directory layout:
#   xlings-<ver>-windows-x86_64/
#   ├── .xlings.json
#   └── bin/
#       └── xlings.exe
#
# Runtime directories are created lazily by `xlings self init`.
#
# Output:  build/xlings-<ver>-windows-x86_64.zip
# Usage:   pwsh ./tools/windows_release.ps1

$ErrorActionPreference = "Stop"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$PROJECT_DIR = (Resolve-Path "$SCRIPT_DIR\..").Path

$VERSION = (Select-String -Path "$PROJECT_DIR\core\config.cppm" -Pattern 'VERSION = "([^"]*)"' |
  ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1)
if (-not $VERSION) { $VERSION = "0.2.0" }

$ARCH = "x86_64"
$PKG_NAME = "xlings-$VERSION-windows-$ARCH"
$OUT_DIR = "$PROJECT_DIR\build\$PKG_NAME"

function Info($msg)  { Write-Host "[release] $msg" }
function Fail($msg)  { Write-Error "[release] FAIL: $msg"; exit 1 }

Set-Location $PROJECT_DIR

# -- 1. Build C++ ------------------------------------------------
Info "Version: $VERSION  |  Arch: $ARCH"
Info "Building C++ binary..."
xmake clean -q 2>$null
xmake build xlings
if ($LASTEXITCODE -ne 0) { Fail "xmake build failed" }

$BIN_SRC = "build\windows\x64\release\xlings.exe"
if (-not (Test-Path $BIN_SRC)) { Fail "C++ binary not found at $BIN_SRC" }

# -- 2. Assemble package -----------------------------------------
Info "Assembling $OUT_DIR ..."
if (Test-Path $OUT_DIR) { Remove-Item -Recurse -Force $OUT_DIR }

$dirs = @(
  "$OUT_DIR\bin"
)
foreach ($d in $dirs) { New-Item -ItemType Directory -Force -Path $d | Out-Null }

Copy-Item $BIN_SRC "$OUT_DIR\bin\"

$configSrc = "$PROJECT_DIR\config\xlings.json"
if (Test-Path $configSrc) {
  $base = Get-Content $configSrc -Raw | ConvertFrom-Json
  $base | Add-Member -NotePropertyName "version" -NotePropertyValue $VERSION -Force
  $base | Add-Member -NotePropertyName "activeSubos" -NotePropertyValue "default" -Force
  $base | Add-Member -NotePropertyName "subos" -NotePropertyValue @{default=@{dir=""}} -Force
  $base | ConvertTo-Json -Depth 10 -Compress | Set-Content "$OUT_DIR\.xlings.json" -Encoding UTF8
} else {
  @"
{"activeSubos":"default","subos":{"default":{"dir":""}},"version":"$VERSION","need_update":false,"mirror":"CN","xim":{"mirrors":{"index-repo":{"GLOBAL":"https://github.com/d2learn/xim-pkgindex.git","CN":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"res-server":{"GLOBAL":"https://github.com/xlings-res","CN":"https://gitcode.com/xlings-res"}},"res-server":"https://gitcode.com/xlings-res","index-repo":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"repo":"https://gitee.com/sunrisepeak/xlings.git"}
"@ | Set-Content "$OUT_DIR\.xlings.json" -Encoding UTF8
}

Info "Package assembled: $OUT_DIR"

# -- 4. Verification ---------------------------------------------
Info "=== Verification ==="

$requiredBins = @("bin\xlings.exe")
foreach ($f in $requiredBins) {
  if (-not (Test-Path "$OUT_DIR\$f")) { Fail "$f is missing" }
}
Info "OK: all binaries present"

if (-not (Test-Path "$OUT_DIR\.xlings.json")) { Fail ".xlings.json missing" }
Info "OK: .xlings.json present"

$env:XLINGS_HOME = $OUT_DIR
$env:PATH = "$OUT_DIR\bin;$env:PATH"

$helpOut = & "$OUT_DIR\bin\xlings.exe" -h 2>&1 | Out-String
if ($helpOut -notmatch "subos") { Fail "xlings -h missing 'subos' command" }
Info "OK: xlings -h shows subos/self commands"

$initOut = & "$OUT_DIR\bin\xlings.exe" self init 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { Fail "xlings self init failed" }
if ($initOut -notmatch "init ok") { Fail "self init output missing success marker" }
$requiredRuntimeDirs = @(
  "data\xpkgs",
  "data\runtimedir",
  "data\xim-index-repos",
  "data\local-indexrepo",
  "subos\default\bin",
  "subos\default\lib",
  "subos\default\usr",
  "subos\default\generations",
  "config\shell"
)
foreach ($d in $requiredRuntimeDirs) {
  if (-not (Test-Path "$OUT_DIR\$d")) { Fail "directory $d missing after self init" }
}
if (-not (Test-Path "$OUT_DIR\subos\current")) { Fail "subos\current junction missing after self init" }
if (-not (Test-Path "$OUT_DIR\subos\default\bin\xlings.exe")) { Fail "subos/default/bin/xlings.exe missing after self init" }
Info "OK: self init materialized bootstrap home"

# -- 5. Create archive -------------------------------------------
Info ""
Info "All checks passed. Creating release archive..."

$ARCHIVE = "$PROJECT_DIR\build\$PKG_NAME.zip"
if (Test-Path $ARCHIVE) { Remove-Item $ARCHIVE }
Compress-Archive -Path $OUT_DIR -DestinationPath $ARCHIVE

Info ""
Info "Done."
Info "  Package:  $OUT_DIR"
Info "  Archive:  $ARCHIVE"
Info ""
Info "  Unpack & install:"
Info "    Expand-Archive $PKG_NAME.zip -DestinationPath ."
Info "    cd $PKG_NAME"
Info "    .\bin\xlings.exe self install"
