# Build a self-contained xlings package for Windows x86_64.
#
# Directory layout matches linux_release.sh (see that file for details).
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

# -- 2. Build xvm (Rust) -----------------------------------------
Info "Building xvm (Rust)..."
Push-Location core\xvm
cargo build --release
if ($LASTEXITCODE -ne 0) { Fail "cargo build failed" }
Pop-Location

$XVM_DIR = "core\xvm\target\release"
if (-not (Test-Path "$XVM_DIR\xvm.exe")) { Fail "xvm.exe not found" }

# -- 3. Assemble package -----------------------------------------
Info "Assembling $OUT_DIR ..."
if (Test-Path $OUT_DIR) { Remove-Item -Recurse -Force $OUT_DIR }

$dirs = @(
  "$OUT_DIR\bin",
  "$OUT_DIR\xim",
  "$OUT_DIR\data\xpkgs",
  "$OUT_DIR\data\runtimedir",
  "$OUT_DIR\data\xim-index-repos",
  "$OUT_DIR\data\local-indexrepo",
  "$OUT_DIR\subos\default\bin",
  "$OUT_DIR\subos\default\lib",
  "$OUT_DIR\subos\default\usr",
  "$OUT_DIR\subos\default\xvm",
  "$OUT_DIR\subos\default\generations",
  "$OUT_DIR\config\i18n",
  "$OUT_DIR\config\shell",
  "$OUT_DIR\tools"
)
foreach ($d in $dirs) { New-Item -ItemType Directory -Force -Path $d | Out-Null }

$defaultAbs = (Resolve-Path "$OUT_DIR\subos\default").Path
New-Item -ItemType Junction -Path "$OUT_DIR\subos\current" -Target $defaultAbs | Out-Null

Copy-Item $BIN_SRC "$OUT_DIR\bin\"
Copy-Item "$XVM_DIR\xvm.exe" "$OUT_DIR\bin\"

if (Test-Path "$XVM_DIR\xvm-shim.exe") {
  Copy-Item "$XVM_DIR\xvm-shim.exe" "$OUT_DIR\bin\"
  foreach ($shim in @("xlings.exe", "xvm.exe", "xvm-shim.exe", "xim.exe", "xinstall.exe", "xsubos.exe", "xself.exe")) {
    Copy-Item "$OUT_DIR\bin\xvm-shim.exe" "$OUT_DIR\subos\default\bin\$shim"
  }
}

@"
---
xvm-wmetadata:
  name: global
  active: true
  inherit: true
versions:
  xlings: bootstrap
  xvm: bootstrap
  xvm-shim: bootstrap
"@ | Set-Content "$OUT_DIR\subos\default\xvm\.workspace.xvm.yaml" -Encoding UTF8

@"
---
xlings:
  bootstrap:
    path: "../../bin"
xvm:
  bootstrap:
    path: "../../bin"
xvm-shim:
  bootstrap:
    path: "../../bin"
"@ | Set-Content "$OUT_DIR\subos\default\xvm\versions.xvm.yaml" -Encoding UTF8

Copy-Item -Recurse "core\xim\*" "$OUT_DIR\xim\" -ErrorAction SilentlyContinue
Copy-Item "config\i18n\*.json" "$OUT_DIR\config\i18n\" -ErrorAction SilentlyContinue
Copy-Item "config\shell\*" "$OUT_DIR\config\shell\" -ErrorAction SilentlyContinue

# Install script (bundled in package root)
Copy-Item "$PROJECT_DIR\tools\install-from-release.ps1" "$OUT_DIR\install.ps1"
'{}' | Set-Content "$OUT_DIR\data\xim-index-repos\xim-indexrepos.json" -Encoding UTF8

$configSrc = "$PROJECT_DIR\config\xlings.json"
if (Test-Path $configSrc) {
  $base = Get-Content $configSrc -Raw | ConvertFrom-Json
  $base | Add-Member -NotePropertyName "activeSubos" -NotePropertyValue "default" -Force
  $base | Add-Member -NotePropertyName "subos" -NotePropertyValue @{default=@{dir=""}} -Force
  $base | ConvertTo-Json -Depth 10 -Compress | Set-Content "$OUT_DIR\.xlings.json" -Encoding UTF8
} else {
  @"
{"activeSubos":"default","subos":{"default":{"dir":""}},"version":"$VERSION","need_update":false,"mirror":"CN","xim":{"mirrors":{"index-repo":{"GLOBAL":"https://github.com/d2learn/xim-pkgindex.git","CN":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"res-server":{"GLOBAL":"https://github.com/xlings-res","CN":"https://gitcode.com/xlings-res"}},"res-server":"https://gitcode.com/xlings-res","index-repo":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"repo":"https://gitee.com/sunrisepeak/xlings.git"}
"@ | Set-Content "$OUT_DIR\.xlings.json" -Encoding UTF8
}

# xmake.lua (package root) — reuse core/xim/xmake.lua which auto-detects layout
Copy-Item "core\xim\xmake.lua" "$OUT_DIR\xmake.lua"

Info "Package assembled: $OUT_DIR"

# -- 4. Verification ---------------------------------------------
Info "=== Verification ==="

$requiredBins = @("bin\xlings.exe", "bin\xvm.exe")
foreach ($f in $requiredBins) {
  if (-not (Test-Path "$OUT_DIR\$f")) { Fail "$f is missing" }
}
Info "OK: all binaries present"

$requiredDirs = @(
  "subos\default\bin", "subos\default\lib", "subos\default\xvm",
  "subos\default\generations", "xim", "data\xpkgs", "config\i18n", "config\shell"
)
foreach ($d in $requiredDirs) {
  if (-not (Test-Path "$OUT_DIR\$d")) { Fail "directory $d missing" }
}
if (-not (Test-Path "$OUT_DIR\subos\current")) { Fail "subos\current junction missing" }
Info "OK: directory structure valid (incl. subos\current junction)"

if (-not (Test-Path "$OUT_DIR\.xlings.json")) { Fail ".xlings.json missing" }
Info "OK: .xlings.json present"

$env:XLINGS_HOME = $OUT_DIR
$env:XLINGS_DATA = "$OUT_DIR\data"
$env:XLINGS_SUBOS = "$OUT_DIR\subos\current"
$env:PATH = "$OUT_DIR\subos\current\bin;$OUT_DIR\bin;$env:PATH"

$helpOut = & "$OUT_DIR\bin\xlings.exe" -h 2>&1 | Out-String
if ($helpOut -notmatch "subos") { Fail "xlings -h missing 'subos' command" }
Info "OK: xlings -h shows subos/self commands"

$xvmOut = & "$OUT_DIR\bin\xvm.exe" --version 2>&1 | Out-String
Info "OK: xvm --version = $($xvmOut.Trim())"

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
Info "    powershell -ExecutionPolicy Bypass -File install.ps1"
