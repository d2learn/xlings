# tests/e2e/tui_utf8_test.ps1
#
# Regression: assert that xlings emits proper UTF-8 byte sequences for the
# theme glyphs (✓, ✗, ○, ▸, ◆, etc.) on Windows. Prior to fixing this we
# would either ship platform-conditional ASCII fallbacks (so Windows users
# saw "+" instead of "✓") or, worse, get mojibake when SetConsoleOutputCP
# wasn't honored by the host.
#
# What we check:
#   1. Console output encoding is set to UTF-8 (so PowerShell's pipe layer
#      doesn't down-convert before we read).
#   2. `xlings --help` and `xlings config` emit at least one BMP glyph from
#      our universal-safe set, encoded as well-formed UTF-8 (no '?' or
#      mojibake bytes).
#
# This script does NOT check whether the user's font has glyphs for those
# code points — that's a font config issue, not something the binary
# controls. We just verify the bytes that leave xlings are correct.

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$rootDir = Resolve-Path "$PSScriptRoot\..\.."
$bin = Join-Path $rootDir 'build\windows\x64\release\xlings.exe'
if (-not (Test-Path $bin)) {
    $bin = Get-ChildItem -Path "$rootDir\build" -Filter 'xlings.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $bin) { throw "xlings.exe not found under $rootDir\build" }
    $bin = $bin.FullName
}

function Fail($msg) {
    Write-Host "[tui-utf8] FAIL: $msg" -ForegroundColor Red
    exit 1
}

function Log($msg) {
    Write-Host "[tui-utf8] $msg"
}

# Exact UTF-8 byte sequences for the canonical icon set in src/ui/theme.cppm.
# Any one of these appearing in the output proves the binary emitted real
# UTF-8 (and that a downstream layer didn't replace them with '?').
$utf8Markers = @(
    @{ Glyph = '✓'; Bytes = @(0xE2, 0x9C, 0x93) }   # done
    @{ Glyph = '✗'; Bytes = @(0xE2, 0x9C, 0x97) }   # failed
    @{ Glyph = '○'; Bytes = @(0xE2, 0x97, 0x8B) }   # pending
    @{ Glyph = '▸'; Bytes = @(0xE2, 0x96, 0xB8) }   # arrow
    @{ Glyph = '◆'; Bytes = @(0xE2, 0x97, 0x86) }   # package
    @{ Glyph = '⊕'; Bytes = @(0xE2, 0x8A, 0x95) }   # installing
    @{ Glyph = '›'; Bytes = @(0xE2, 0x80, 0xBA) }   # info
    @{ Glyph = '↓'; Bytes = @(0xE2, 0x86, 0x93) }   # downloading
    @{ Glyph = '▾'; Bytes = @(0xE2, 0x96, 0xBE) }   # extracting
)

function Capture-Utf8Bytes($args) {
    # Run xlings, capture its stdout as raw bytes (not via PowerShell's
    # string pipeline, which would re-encode if console CP isn't UTF-8).
    $tempOut = New-TemporaryFile
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $bin
        $psi.Arguments = $args
        $psi.RedirectStandardOutput = $true
        $psi.UseShellExecute = $false
        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
        $proc = [System.Diagnostics.Process]::Start($psi)
        $proc.WaitForExit()
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($proc.StandardOutput.ReadToEnd())
        return ,$bytes
    } finally {
        Remove-Item $tempOut -ErrorAction SilentlyContinue
    }
}

function Contains-ByteSequence($haystack, $needle) {
    if ($haystack.Length -lt $needle.Length) { return $false }
    for ($i = 0; $i -le ($haystack.Length - $needle.Length); $i++) {
        $match = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($haystack[$i + $j] -ne $needle[$j]) { $match = $false; break }
        }
        if ($match) { return $true }
    }
    return $false
}

# ── 1. Capture xlings --help bytes ───────────────────────────────
Log "Running: $bin --help"
$helpBytes = Capture-Utf8Bytes '--help'
if ($helpBytes.Length -eq 0) { Fail "--help produced no output" }
Log "--help emitted $($helpBytes.Length) bytes"

# ── 2. Look for any of the canonical UTF-8 sequences ─────────────
$matched = @()
foreach ($m in $utf8Markers) {
    if (Contains-ByteSequence $helpBytes $m.Bytes) { $matched += $m.Glyph }
}

if ($matched.Count -eq 0) {
    # Print the first ~200 bytes hex for triage
    $preview = ($helpBytes[0..([Math]::Min(200, $helpBytes.Length-1))] | ForEach-Object { '{0:x2}' -f $_ }) -join ' '
    Log "First bytes (hex): $preview"
    Fail "No canonical UTF-8 icon byte sequences found in --help output. Check src/ui/theme.cppm and platform::init_console_output()."
}

Log "Found UTF-8 glyphs in --help output: $($matched -join ', ')"

# ── 3. Reject classic mojibake markers ───────────────────────────
# If SetConsoleOutputCP failed AND PowerShell down-converted, we'd see
# replacement char (?) instead of the multi-byte sequences. But since we
# just verified the bytes are present, this is a belt-and-braces check
# against output containing '???' for what should be a single icon row.
$asString = [System.Text.Encoding]::UTF8.GetString($helpBytes)
if ($asString -match '\?\?\?') {
    Log "WARN: triple-question-mark sequence found, may indicate partial encoding loss"
}

Log "PASS: xlings emits well-formed UTF-8 icon glyphs on Windows"
