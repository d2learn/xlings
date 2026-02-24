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

XLINGS_HOME="$HOME/.xlings"

case "$OS_TYPE" in
    linux)
        PROFILE_FILES=("$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile")
        ;;
    macos)
        PROFILE_FILES=("$HOME/.zshrc" "$HOME/.bashrc" "$HOME/.zprofile")
        ;;
    *)
        log_error "Unsupported OS: $(uname -s)"
        exit 1
        ;;
esac

log_info "Installing xlings to ${CYAN}${XLINGS_HOME}${RESET}"

# Create XLINGS_HOME if it doesn't exist
mkdir -p "$XLINGS_HOME"

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

# Add to shell profile
PROFILE_LINE="export PATH=\"$XLINGS_BIN:\$PATH\""
PROFILE_ADDED=false

for profile in "${PROFILE_FILES[@]}"; do
    if [[ -f "$profile" ]]; then
        if ! grep -qF "$XLINGS_BIN" "$profile" 2>/dev/null; then
            echo "" >> "$profile"
            echo "# xlings" >> "$profile"
            echo "$PROFILE_LINE" >> "$profile"
            log_info "Added PATH to ${CYAN}${profile}${RESET}"
        else
            log_info "PATH already configured in ${profile}"
        fi
        PROFILE_ADDED=true
        break
    fi
done

if [[ "$PROFILE_ADDED" == "false" ]]; then
    log_warn "No shell profile found. Manually add to your profile:"
    log_warn "  $PROFILE_LINE"
fi

# Update PATH for current session
export PATH="$XLINGS_BIN:$XLINGS_HOME/bin:$PATH"

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
