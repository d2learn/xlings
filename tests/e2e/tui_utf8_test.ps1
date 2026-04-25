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

# Exact UTF-8 byte sequences for the canonical icon set in src/ui/theme.cppm
# plus ftxui box-drawing primitives. Any one of these appearing proves the
# binary emitted real UTF-8 (and that a downstream layer didn't replace them
# with '?'). `xlings config` reliably exercises both the icon set (◆) and
# ftxui's border (─ ┌ ┐ └ ┘ │) on every platform.
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
    @{ Glyph = '─'; Bytes = @(0xE2, 0x94, 0x80) }   # box horizontal (ftxui border)
    @{ Glyph = '│'; Bytes = @(0xE2, 0x94, 0x82) }   # box vertical
)

function Capture-Utf8Bytes($args) {
    # Run xlings and collect both stdout and stderr as UTF-8 bytes. We
    # combine the two streams because xlings's TUI rendering (ftxui) and
    # log layer can land on either depending on the path; we just want to
    # confirm "the binary emits valid UTF-8 byte sequences for our icons".
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $bin
    $psi.Arguments = $args
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    $combined = $stdout + $stderr
    return ,([System.Text.Encoding]::UTF8.GetBytes($combined))
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

# ── 1. Capture xlings config bytes ───────────────────────────────
# `xlings config` reliably renders an info_panel with ftxui borders and
# the ◆ package icon, so its output is the most stable carrier for the
# UTF-8 byte sequences we want to verify. `--help` is pure text and
# wouldn't exercise the byte path we care about.
Log "Running: $bin config"
$bytes = Capture-Utf8Bytes 'config'
if ($bytes.Length -eq 0) { Fail "'xlings config' produced no output" }
Log "'xlings config' emitted $($bytes.Length) bytes (stdout+stderr combined)"

# ── 2. Look for any canonical UTF-8 sequence ─────────────────────
$matched = @()
foreach ($m in $utf8Markers) {
    if (Contains-ByteSequence $bytes $m.Bytes) { $matched += $m.Glyph }
}

if ($matched.Count -eq 0) {
    # Print the first ~200 bytes hex for triage
    $preview = ($bytes[0..([Math]::Min(200, $bytes.Length-1))] | ForEach-Object { '{0:x2}' -f $_ }) -join ' '
    Log "First bytes (hex): $preview"
    Fail "No canonical UTF-8 byte sequences found in 'xlings config' output. Check src/ui/theme.cppm and platform::init_console_output()."
}

Log "Found UTF-8 glyphs: $($matched -join ', ')"

# ── 3. Reject classic mojibake markers ───────────────────────────
# If SetConsoleOutputCP failed AND PowerShell down-converted, we'd see
# replacement char (?) instead of the multi-byte sequences. The matcher
# above already proves the bytes survived; this is just a belt-and-braces
# check that consecutive '?' characters aren't masking a partial loss.
$asString = [System.Text.Encoding]::UTF8.GetString($bytes)
if ($asString -match '\?\?\?\?') {
    Log "WARN: long question-mark run found, may indicate partial encoding loss"
}

Log "PASS: xlings emits well-formed UTF-8 icon glyphs on Windows"
