#!/usr/bin/env bash
# One-line installer for xlings (Linux / macOS).
#   curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.sh | bash

set -euo pipefail

RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
CYAN='\033[36m'
RESET='\033[0m'

log_info()  { echo -e "${GREEN}[xlings]:${RESET} $1"; }
log_warn()  { echo -e "${YELLOW}[xlings]:${RESET} $1"; }
log_error() { echo -e "${RED}[xlings]:${RESET} $1"; }

trap 'log_error "Interrupted"; exit 1' INT TERM

GITHUB_REPO="d2learn/xlings"
GITHUB_MIRROR="${XLINGS_GITHUB_MIRROR:-}"

# --------------- detect platform ---------------

detect_os() {
    case "$(uname -s)" in
        Linux*)  echo "linux" ;;
        Darwin*) echo "macos" ;;
        *)       echo "unknown" ;;
    esac
}

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)   echo "x86_64" ;;
        aarch64|arm64)  echo "arm64" ;;
        *)              echo "unknown" ;;
    esac
}

OS_TYPE=$(detect_os)
ARCH_TYPE=$(detect_arch)

if [[ "$OS_TYPE" == "unknown" ]]; then
    log_error "Unsupported OS: $(uname -s)"
    exit 1
fi

if [[ "$ARCH_TYPE" == "unknown" ]]; then
    log_error "Unsupported architecture: $(uname -m)"
    exit 1
fi

case "$OS_TYPE" in
    linux) PLATFORM="linux" ;;
    macos) PLATFORM="macosx" ;;
esac

# --------------- banner ---------------

cat << 'EOF'

 __   __  _      _
 \ \ / / | |    (_)
  \ V /  | |     _  _ __    __ _  ___
   > <   | |    | || '_ \  / _  |/ __|
  / . \  | |____| || | | || (_| |\__ \
 /_/ \_\ |______|_||_| |_| \__, ||___/
                            __/ |
                           |___/

repo:  https://github.com/d2learn/xlings
forum: https://forum.d2learn.org

EOF

# --------------- resolve download URL ---------------

ensure_cmd() {
    if ! command -v "$1" &>/dev/null; then
        log_error "'$1' is required but not found. Please install it first."
        exit 1
    fi
}

ensure_cmd curl
ensure_cmd tar

LATEST_VERSION=$(curl -fsSL "https://api.github.com/repos/${GITHUB_REPO}/releases/latest" \
    | grep '"tag_name"' | head -1 | sed -E 's/.*"tag_name":\s*"([^"]+)".*/\1/')

if [[ -z "$LATEST_VERSION" ]]; then
    log_error "Failed to query the latest release version."
    exit 1
fi

VERSION_NUM="${LATEST_VERSION#v}"
TARBALL="xlings-${VERSION_NUM}-${PLATFORM}-${ARCH_TYPE}.tar.gz"

if [[ -n "$GITHUB_MIRROR" ]]; then
    DOWNLOAD_URL="${GITHUB_MIRROR}/${GITHUB_REPO}/releases/download/${LATEST_VERSION}/${TARBALL}"
else
    DOWNLOAD_URL="https://github.com/${GITHUB_REPO}/releases/download/${LATEST_VERSION}/${TARBALL}"
fi

log_info "Latest version: ${CYAN}${LATEST_VERSION}${RESET}"
log_info "Package:        ${CYAN}${TARBALL}${RESET}"
log_info "Download URL:   ${CYAN}${DOWNLOAD_URL}${RESET}"

# --------------- download & extract ---------------

TMPDIR_ROOT="${TMPDIR:-/tmp}"
WORK_DIR=$(mktemp -d "${TMPDIR_ROOT}/xlings-install.XXXXXX")

cleanup() {
    log_info "Cleaning up temporary files..."
    rm -rf "$WORK_DIR"
}
trap 'cleanup; log_error "Interrupted"; exit 1' INT TERM
trap cleanup EXIT

log_info "Downloading..."
if ! curl -fSL --progress-bar -o "${WORK_DIR}/${TARBALL}" "$DOWNLOAD_URL"; then
    log_error "Download failed. Please check your network or try setting XLINGS_GITHUB_MIRROR."
    exit 1
fi

log_info "Extracting..."
tar -xzf "${WORK_DIR}/${TARBALL}" -C "$WORK_DIR"

EXTRACT_DIR=$(find "$WORK_DIR" -mindepth 1 -maxdepth 1 -type d -name "xlings-*" | head -1)
if [[ -z "$EXTRACT_DIR" ]] || [[ ! -x "$EXTRACT_DIR/bin/xlings" && ! -f "$EXTRACT_DIR/bin/xlings" ]]; then
    log_error "Extracted package is invalid (missing bin/xlings)."
    exit 1
fi

# --------------- run installer ---------------

log_info "Running installer..."
cd "$EXTRACT_DIR"
chmod +x bin/xlings
# Use /dev/tty for interactive prompts when piped (curl|bash); set XLINGS_NON_INTERACTIVE=1 in CI
if [[ -n "${XLINGS_NON_INTERACTIVE:-}" ]]; then
  ./bin/xlings self install
elif [[ -e /dev/tty ]] && [[ -r /dev/tty ]]; then
  ./bin/xlings self install < /dev/tty
else
  ./bin/xlings self install
fi
