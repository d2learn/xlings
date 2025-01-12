#!/bin/bash

RUN_DIR=`pwd`

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
    curl -fsSL https://xmake.io/shget.text | bash
    source ~/.xmake/profile
else
    echo -e "${GREEN}[xlings]: xmake installed${RESET}"
fi

if [ -f $RUN_DIR/install.unix.sh ]; then
    cd ..
    RUN_DIR=`pwd`
fi

# 2. install xlings
cd $RUN_DIR/core
xmake xlings unused install xlings

export PATH="$HOME/.xlings_data/bin:$PATH"

# 3. install xvm
xlings install -i xvm -y

# 4. config xlings by xvm
xvm add xim 0.0.2 --alias "xlings install"
xvm add xinstall 0.0.2 --alias "xlings install"
xvm add xrun 0.0.2 --alias "xlings run"
xvm add xchecker 0.0.2 --alias "xlings checker"

# 5. install info
echo -e "${GREEN}[xlings]: xlings installed${RESET}"

echo -e ""
echo -e "\t run $YELLOW xlings help $RESET get more information"
echo -e "\t after run $YELLOW source ~/.bashrc $RESET to reload"
echo -e ""

cd $RUN_DIR

# if $1 is nil
if [ -n "$1" ] && [ "$1" = "disable_reopen" ]; then
    echo -e "[xlings]: exec bash - disable reopen"
else
    exec bash # update env
fi