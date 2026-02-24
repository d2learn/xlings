#!/usr/bin/env bash
# Install xlings from a self-contained release package.
# Run this script from inside the extracted release directory:
#   tar -xzf xlings-<ver>-linux-x86_64.tar.gz
#   cd xlings-<ver>-linux-x86_64
#   ./install.sh

set -euo pipefail

RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
CYAN='\033[36m'
RESET='\033[0m'

log_info()    { echo -e "${GREEN}[xlings]:${RESET} $1"; }
log_warn()    { echo -e "${YELLOW}[xlings]:${RESET} $1"; }
log_error()   { echo -e "${RED}[xlings]:${RESET} $1"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Sanity check: we must be inside a valid release package
if [[ ! -d "bin" ]] || [[ ! -d "subos" ]] || [[ ! -d "xim" ]]; then
    log_error "This does not look like a valid xlings release package."
    log_error "Expected bin/, subos/, xim/ directories in: $SCRIPT_DIR"
    exit 1
fi

detect_os() {
    case "$(uname -s)" in
        Linux*)  echo "linux" ;;
        Darwin*) echo "macos" ;;
        *)       echo "unknown" ;;
    esac
}

OS_TYPE=$(detect_os)

case "$OS_TYPE" in
    linux)
        DEFAULT_XLINGS_HOME="$HOME/.xlings"
        PROFILE_FILES=("$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile")
        ;;
    macos)
        DEFAULT_XLINGS_HOME="$HOME/.xlings"
        PROFILE_FILES=("$HOME/.zshrc" "$HOME/.bashrc" "$HOME/.zprofile")
        ;;
    *)
        log_error "Unsupported OS: $(uname -s)"
        exit 1
        ;;
esac

detect_existing_xlings_home() {
    local existing_bin
    existing_bin=$(command -v xlings 2>/dev/null) || return 1
    local bin_dir
    bin_dir=$(cd "$(dirname "$existing_bin")" 2>/dev/null && pwd) || return 1
    local candidate
    candidate=$(cd "$bin_dir/../../.." 2>/dev/null && pwd) || return 1
    if [[ -d "$candidate/bin" ]] && [[ -d "$candidate/subos" ]]; then
        echo "$candidate"
    fi
}

OLD_XLINGS_HOME=""
if [[ -n "${XLINGS_HOME:-}" ]] && [[ "${XLINGS_HOME}" != "$DEFAULT_XLINGS_HOME" ]]; then
    OLD_XLINGS_HOME="$XLINGS_HOME"
fi

if [[ -z "$OLD_XLINGS_HOME" ]]; then
    OLD_XLINGS_HOME=$(detect_existing_xlings_home || true)
fi

if [[ -n "$OLD_XLINGS_HOME" ]] && [[ "$OLD_XLINGS_HOME" != "$DEFAULT_XLINGS_HOME" ]]; then
    log_warn "Detected existing xlings at: ${CYAN}${OLD_XLINGS_HOME}${RESET}"
    log_warn "Default install directory is: ${CYAN}${DEFAULT_XLINGS_HOME}${RESET}"
    echo ""
    echo -e "  [1] Overwrite existing installation at ${CYAN}${OLD_XLINGS_HOME}${RESET}"
    echo -e "  [2] Install to default location ${CYAN}${DEFAULT_XLINGS_HOME}${RESET} (keep old)"
    echo ""
    read -rp "Choose [1/2] (default: 2): " choice
    case "$choice" in
        1)
            XLINGS_HOME="$OLD_XLINGS_HOME"
            ;;
        *)
            XLINGS_HOME="$DEFAULT_XLINGS_HOME"
            ;;
    esac
else
    XLINGS_HOME="$DEFAULT_XLINGS_HOME"
fi

log_info "Installing xlings to ${CYAN}${XLINGS_HOME}${RESET}"

# Create XLINGS_HOME if it doesn't exist
if [[ "$UID" -eq 0 ]]; then
    mkdir -p "$XLINGS_HOME"
else
    if [[ ! -d "$XLINGS_HOME" ]]; then
        if ! mkdir -p "$XLINGS_HOME" 2>/dev/null; then
            log_warn "Cannot create $XLINGS_HOME without root. Using sudo..."
            sudo mkdir -p "$XLINGS_HOME"
            sudo chown "$(id -u):$(id -g)" "$XLINGS_HOME"
        fi
    fi
fi

# Copy package contents to XLINGS_HOME
if [[ "$SCRIPT_DIR" != "$XLINGS_HOME" ]]; then
    log_info "Copying package to $XLINGS_HOME ..."
    # Use rsync if available for better reliability, fallback to cp
    if command -v rsync &>/dev/null; then
        rsync -a --delete "$SCRIPT_DIR/" "$XLINGS_HOME/"
    else
        rm -rf "${XLINGS_HOME:?}/"*
        cp -a "$SCRIPT_DIR/." "$XLINGS_HOME/"
    fi
else
    log_info "Already running from $XLINGS_HOME, skipping copy."
fi

# Fix permissions
chmod +x "$XLINGS_HOME/bin/"* 2>/dev/null || true
chmod +x "$XLINGS_HOME/subos/default/bin/"* 2>/dev/null || true

# Recreate subos/current symlink (tar doesn't always preserve them correctly)
if [[ -e "$XLINGS_HOME/subos/current" ]]; then
    rm -f "$XLINGS_HOME/subos/current"
fi
ln -sfn default "$XLINGS_HOME/subos/current"

XLINGS_BIN="$XLINGS_HOME/subos/current/bin"

XLINGS_PROFILE_SH="$XLINGS_HOME/config/shell/xlings-profile.sh"
XLINGS_PROFILE_FISH="$XLINGS_HOME/config/shell/xlings-profile.fish"
PROFILE_ADDED=false

# bash / zsh
SOURCE_LINE="test -f \"$XLINGS_PROFILE_SH\" && source \"$XLINGS_PROFILE_SH\""

for profile in "${PROFILE_FILES[@]}"; do
    if [[ -f "$profile" ]]; then
        if ! grep -qF "xlings-profile" "$profile" 2>/dev/null; then
            echo "" >> "$profile"
            echo "# xlings" >> "$profile"
            echo "$SOURCE_LINE" >> "$profile"
            log_info "Added xlings profile to ${CYAN}${profile}${RESET}"
        else
            log_info "xlings profile already configured in ${profile}"
        fi
        PROFILE_ADDED=true
        break
    fi
done

# fish
FISH_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/fish/config.fish"
if [[ -f "$FISH_CONFIG" ]] || command -v fish &>/dev/null; then
    mkdir -p "$(dirname "$FISH_CONFIG")"
    FISH_SOURCE_LINE="test -f \"$XLINGS_PROFILE_FISH\"; and source \"$XLINGS_PROFILE_FISH\""
    if [[ -f "$FISH_CONFIG" ]] && grep -qF "xlings-profile" "$FISH_CONFIG" 2>/dev/null; then
        log_info "xlings profile already configured in ${FISH_CONFIG}"
    else
        echo "" >> "$FISH_CONFIG"
        echo "# xlings" >> "$FISH_CONFIG"
        echo "$FISH_SOURCE_LINE" >> "$FISH_CONFIG"
        log_info "Added xlings profile to ${CYAN}${FISH_CONFIG}${RESET}"
    fi
    PROFILE_ADDED=true
fi

if [[ "$PROFILE_ADDED" == "false" ]]; then
    log_warn "No shell profile found. Manually add to your shell config:"
    log_warn "  $SOURCE_LINE"
fi

# Apply profile for current session
source "$XLINGS_PROFILE_SH"

# Verify
if "$XLINGS_HOME/bin/xlings" -h &>/dev/null; then
    log_info "Verification passed."
else
    log_warn "xlings binary test failed. The package may need platform-specific dependencies."
fi

log_info "${GREEN}xlings installed successfully!${RESET}"
echo ""
echo -e "    Run [${YELLOW}xlings -h${RESET}] to get started."
echo -e "    Restart your shell or run: ${CYAN}source ${PROFILE_FILES[0]:-~/.bashrc}${RESET}"
echo ""
