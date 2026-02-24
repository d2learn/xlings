#!/bin/bash

set -euo pipefail

# 颜色定义
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
PURPLE='\033[35m'
RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="${PWD}"

# 配置
XMAKE_BIN_URL_LINUX="https://gitee.com/sunrisepeak/xlings-pkg/raw/master/xmake-3.0.0-linux-x86_64"
XMAKE_BIN_URL_MACOS="https://gitee.com/sunrisepeak/xlings-pkg/raw/master/xmake-3.0.0-macosx-arm64"
XMAKE_LOCAL="$ROOT_DIR/bin/xmake"

XLINGS_HOME_LINUX="/home/xlings"
XLINGS_HOME_MACOS="/Users/xlings"
XLINGS_SYMLINK_LINUX="/usr/bin/xlings"
XLINGS_SYMLINK_MACOS="/usr/local/bin/xlings"

# 检测系统
detect_os() {
    case "$(uname -s)" in
        Linux*)  echo "linux" ;;
        Darwin*) echo "macos" ;;
        *)       echo "unknown" ;;
    esac
}

OS_TYPE=$(detect_os)

# 根据系统设置变量
case "$OS_TYPE" in
    linux)
        XLINGS_HOME="$XLINGS_HOME_LINUX"
        XLINGS_SYMLINK="$XLINGS_SYMLINK_LINUX"
        XMAKE_BIN_URL="$XMAKE_BIN_URL_LINUX"
        ;;
    macos)
        XLINGS_HOME="$XLINGS_HOME_MACOS"
        XLINGS_SYMLINK="$XLINGS_SYMLINK_MACOS"
        XMAKE_BIN_URL="$XMAKE_BIN_URL_MACOS"
        ;;
    *)
        echo -e "${RED}[xlings]: Unsupported OS${RESET}"
        exit 1
        ;;
esac

log_info() { echo -e "${PURPLE}[xlings]: $1${RESET}"; }
log_success() { echo -e "${GREEN}[xlings]: $1${RESET}"; }
log_warn() { echo -e "${YELLOW}[xlings]: $1${RESET}"; }
log_error() { echo -e "${RED}[xlings]: $1${RESET}"; }

trap 'log_error "Interrupted"; exit 1' INT TERM

# 1. 检查并安装 xmake
if command -v xmake &> /dev/null; then
    log_success "xmake already installed"
    XMAKE_BIN="xmake"
else
    log_info "installing xmake..."
    mkdir -p "$ROOT_DIR/bin"

    if ! curl -sSL --fail "$XMAKE_BIN_URL" -o "$XMAKE_LOCAL"; then
        log_error "Failed to download xmake"
        rm -f "$XMAKE_LOCAL"
        exit 1
    fi

    chmod +x "$XMAKE_LOCAL"

    if [[ "$OS_TYPE" == "macos" ]]; then
        xattr -d com.apple.quarantine "$XMAKE_LOCAL" 2>/dev/null || true
    fi

    XMAKE_BIN="$XMAKE_LOCAL"
fi

# 2. 处理权限
if [[ "$UID" -eq 0 ]]; then
    export XMAKE_ROOT=y
fi

# 3. 安装 xlings
cd "$ROOT_DIR/core"
"$XMAKE_BIN" xlings --project=. unused self enforce-install

# 4. 创建软链接
if [[ "$UID" -eq 0 ]]; then
    ln -sf "$XLINGS_HOME/data/bin/xlings" "$XLINGS_SYMLINK"
fi

export PATH="$XLINGS_HOME/data/bin:$PATH"

# 5. 检查 xlings 命令是否可用
if ! command -v xlings &> /dev/null; then
    log_error "xlings command not found, installation failed"
    exit 1
fi

# 6. 初始化
xlings self init

# 7. 完成信息
log_success "xlings installed"
echo ""
echo -e "    run [${YELLOW}xlings help${RESET}] get more information"
echo -e "    after restart ${YELLOW}cmd/shell${RESET} to refresh environment"
echo ""

cd "$RUN_DIR"
