#!/bin/bash

RUN_DIR=`pwd`
#SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)" # avoid source script.sh issue
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
#XMAKE_BIN_URL=https://github.com/xmake-io/xmake/releases/download/v2.9.9/xmake-bundle-v2.9.9.linux.x86_64
XMAKE_BIN_URL=https://gitcode.com/xlings-res/xmake/releases/download/2.9.9/xmake-2.9.9-linux-x86_64
XMAKE_BIN="xmake"

XLINGS_HOME="/home/xlings"
XLINGS_SYMLINK="/usr/bin/xlings"

trap "echo 'Ctrl+C or killed...'; exit 1" INT TERM

if [ "$(uname)" == "Darwin" ]; then
    XLINGS_HOME="/Users/xlings"
    XLINGS_SYMLINK="/usr/local/bin/xlings"
    XMAKE_BIN_URL=https://gitcode.com/xlings-res/xmake/releases/download/2.9.9/xmake-2.9.9-macos-arm64
fi

# ANSI color codes
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
BLUE='\033[34m'
PURPLE='\033[35m'
CYAN='\033[36m'
RESET='\033[0m'

echo -e "${PURPLE}[xlings]: start detect environment and try to auto config...${RESET}"

# 1. check xmake status
if ! command -v xmake &> /dev/null
then
    echo -e "${PURPLE}[xlings]: start install xmake...${RESET}"
    XMAKE_BIN="$ROOT_DIR/bin/xmake"
    curl -sSL $XMAKE_BIN_URL -o $XMAKE_BIN
    chmod +x $XMAKE_BIN
    if [ "$(uname)" == "Darwin" ]; then
        xattr -d com.apple.quarantine $XMAKE_BIN
    fi
else
    echo -e "${GREEN}[xlings]: xmake installed${RESET}"
fi

if [ -f $RUN_DIR/install.unix.sh ]; then
    cd ..
    RUN_DIR=`pwd`
fi

if [ "$UID" -eq 0 ];
then
    export XMAKE_ROOT=y
fi

# 2. install xlings
cd $ROOT_DIR/core
$XMAKE_BIN xlings unused self enforce-install
sudo ln -sf $XLINGS_HOME/.xlings_data/bin/xlings $XLINGS_SYMLINK

export PATH="$XLINGS_HOME/.xlings_data/bin:$PATH"

# 3. init: install xvm and create xim, xinstall...
xlings self init

# 5. install info
echo -e "${GREEN}[xlings]: xlings installed${RESET}"

echo -e ""
echo -e "\t    run [$YELLOW xlings help $RESET] get more information"
echo -e "\t after restart $YELLOW cmd/shell $RESET to refresh environment"
echo -e ""

cd $RUN_DIR
