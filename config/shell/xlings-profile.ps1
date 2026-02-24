# Xlings Shell Profile (PowerShell)

$env:XLINGS_HOME = (Resolve-Path "$PSScriptRoot\..\..").Path

$env:XLINGS_BIN = "$env:XLINGS_HOME\subos\current\bin"

if ($env:Path -notlike "*$env:XLINGS_BIN*") {
    $env:Path = "$env:XLINGS_BIN;$env:XLINGS_HOME\bin;$env:Path"
}