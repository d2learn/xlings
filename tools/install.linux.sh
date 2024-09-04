#!/bin/bash

XLINGS_INSTALL_DIR=~/.xlings
XLINGS_BIN_DIR=$XLINGS_INSTALL_DIR/bin

# ANSI color codes
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
BLUE='\033[34m'
PURPLE='\033[35m'
CYAN='\033[36m'
RESET='\033[0m'

# 0. rm old xlings
if [ -d $XLINGS_INSTALL_DIR ]; then
    echo -e "${PURPLE}[xlings]: delete xlings(old version)...${RESET}"
    rm -r $XLINGS_INSTALL_DIR
fi

mkdir -p $XLINGS_BIN_DIR

# 1. install xlings
echo -e "${PURPLE}[xlings]: install xlings to $XLINGS_INSTALL_DIR ${RESET}"
cp -r ./* $XLINGS_INSTALL_DIR
cp $XLINGS_BIN_DIR/xlings.linux.sh $XLINGS_BIN_DIR/xlings
chmod +x $XLINGS_BIN_DIR/xlings

# 2. check xmake status
echo -e "${PURPLE}[xlings]: start detect environment and try to auto config...${RESET}"
if ! command -v xmake &> /dev/null
then
    echo -e "${PURPLE}[xlings]: start install xmake...${RESET}"
    curl -fsSL https://xmake.io/shget.text | bash
else
    echo -e "${GREEN}[xlings]: xmake installed${RESET}"
fi

# 3. check/install mdbook
if ! command -v mdbook &> /dev/null
then
    echo -e "${PURPLE}[xlings]: start install mdbook...${RESET}"

    if ! [ -f $XLINGS_BIN_DIR/mdbook ]; then

        # get latest release url
        release_url=$(curl -s https://api.github.com/repos/rust-lang/mdBook/releases/latest | grep "browser_download_url.*x86_64-unknown-linux-gnu.tar.gz" | cut -d '"' -f 4)

        # download
        curl -L $release_url -o ~/.xlings/mdbook.tar.gz

        # decompress
        tar -xzf ~/.xlings/mdbook.tar.gz -C ~/.xlings/bin

        # delete tmp-file
        rm ~/.xlings/mdbook.tar.gz
    fi

    # add xlings and tools to PATH
    if ! grep -q "$XLINGS_BIN_DIR" ~/.bashrc; then
        # mdbook
        echo "" >> ~/.bashrc
        echo "# xlings config" >> ~/.bashrc
        echo "export PATH=\$PATH:$XLINGS_BIN_DIR" >> ~/.bashrc
        echo -e "${PURPLE}[xlings]: config PATH (~/.bashrc)${RESET}"
    fi

fi
echo -e "${GREEN}[xlings]: mdbook installed${RESET}"

# 4. install info
echo -e "${GREEN}[xlings]: xlings installed${RESET}"

echo -e $YELLOW
echo -e "\t run xlings --help get more information"
echo -e $RESET