# xlings Tests

This directory contains end-to-end usability tests for `xlings`.

## E2E Linux Usability Test

Script: `tests/e2e/linux_usability_test.sh`

What it validates in an isolated extracted release package:

- basic CLI availability (`xlings -h`, `xlings config`, `xvm --version`)
- `xlings info <target>` mapping to `xvm info`
- `subos` lifecycle (`new`, `use`, `list`, `info`, `remove`)
- new `subos` short aliases (`ls`, `i`, `rm`)
- backward compatibility for existing `subos` commands
- self-maintenance dry-run command
- optional network install scenario (`xlings install d2x`)

### Run

```bash
# auto-detect latest build/xlings-*-linux-x86_64.tar.gz
bash tests/e2e/linux_usability_test.sh

# specify archive explicitly
bash tests/e2e/linux_usability_test.sh build/xlings-0.2.0-linux-x86_64.tar.gz
```

### Network Scenario

By default network-dependent installation checks are skipped.

```bash
SKIP_NETWORK_TESTS=0 bash tests/e2e/linux_usability_test.sh
```

## E2E macOS Usability Test

Script: `tests/e2e/macos_usability_test.sh`

```bash
bash tests/e2e/macos_usability_test.sh
SKIP_NETWORK_TESTS=0 bash tests/e2e/macos_usability_test.sh
```

## E2E Windows Usability Test

Script: `tests/e2e/windows_usability_test.ps1`

```powershell
powershell -ExecutionPolicy Bypass -File tests/e2e/windows_usability_test.ps1
$env:SKIP_NETWORK_TESTS = "0"
powershell -ExecutionPolicy Bypass -File tests/e2e/windows_usability_test.ps1
```

