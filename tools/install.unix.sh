#!/bin/bash

RUN_DIR=`pwd`
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
XMAKE_BIN="$ROOT_DIR/bin/xmake"

XLINGS_HOME="/home/xlings"
XLINGS_SYMLINK="/usr/bin/xlings"

if [ "$(uname)" = "Darwin" ]; then
    XLINGS_HOME="/Users/xlings"
    XLINGS_SYMLINK="/usr/local/bin/xlings"
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
    curl -L https://github.com/xlings-res/xmake/releases/download/2.9.9/xmake -o $XMAKE_BIN
    chmod +x $XMAKE_BIN
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
xmake xlings unused self enforce-install
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