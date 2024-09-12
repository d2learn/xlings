#!/bin/bash

XLINGS_DIR=`pwd`

# ANSI color codes
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
BLUE='\033[34m'
PURPLE='\033[35m'
CYAN='\033[36m'
RESET='\033[0m'

echo -e "${PURPLE}[xlings]: start detect environment and try to auto config...${RESET}"

# 0. check curl status
if ! command -v curl &> /dev/null
then
    echo -e "${PURPLE}[xlings]: start install curl...${RESET}"
    curl -fsSL https://xmake.io/shget.text | bash
else
    echo -e "${GREEN}[xlings]: curl installed${RESET}"
fi


# 1. check xmake status
if ! command -v xmake &> /dev/null
then
    echo -e "${PURPLE}[xlings]: start install xmake...${RESET}"
    curl -fsSL https://xmake.io/shget.text | bash
else
    echo -e "${GREEN}[xlings]: xmake installed${RESET}"
fi

source ~/.bashrc

# 2. install xlings
cd $XLINGS_DIR/core
xmake xlings unused install

# 3. install info
echo -e "${GREEN}[xlings]: xlings installed${RESET}"

echo -e ""
echo -e "\t run $YELLOW xlings help $RESET get more information"
echo -e ""