#!/bin/bash

RUN_DIR=`pwd`
SOFTWARE_URL="https://github.com/d2learn/xlings/archive/refs/heads/main.zip"
ZIP_FILE="software.zip"
INSTALL_DIR=".xlings_software_install"
XLINGS_DIR="xlings-main"
INSTALL_SCRIPT="tools/install.unix.sh"

install_tool() {
    tool=$1
    if command -v apt-get &> /dev/null; then
        sudo apt-get update && sudo apt-get install -y $tool
    elif command -v yum &> /dev/null; then
        sudo yum install -y $tool
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y $tool
    elif command -v zypper &> /dev/null; then
        sudo zypper install -y $tool
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --noconfirm $tool
    elif command -v brew &> /dev/null; then
        brew install $tool
    else
        echo "Unable to install $tool. Please install it manually."
        exit 1
    fi
}

check_and_install_tool() {
    tool=$1
    if ! command -v $tool &> /dev/null; then
        echo "$tool is not installed. Attempting to install..."
        install_tool $tool
        if ! command -v $tool &> /dev/null; then
            echo "Failed to install $tool. Please install it manually and try again."
            exit 1
        fi
    fi
}

if ! command -v curl &> /dev/null && ! command -v wget &> /dev/null; then
    check_and_install_tool curl
fi

if ! command -v unzip &> /dev/null && ! command -v jar &> /dev/null && ! command -v 7z &> /dev/null; then
    check_and_install_tool unzip
fi


# ------------------------------

# remove old dir
if [ -d "$INSTALL_DIR" ]; then
    rm -rf "$INSTALL_DIR"
fi

# create tmp dir
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

echo "Downloading xlings..."
curl -L -o "$ZIP_FILE" "$SOFTWARE_URL"

# check download status
if [ $? -ne 0 ]; then
    echo "Failed to download the software. Please check your internet connection and try again."
    exit 1
fi

echo "Extracting files..."
unzip -q "$ZIP_FILE"

if [ $? -ne 0 ]; then
    echo "Failed to extract the ZIP file. The file might be corrupted."
    exit 1
fi

echo "Running install.unix.sh..."
cd "$XLINGS_DIR"
source $INSTALL_SCRIPT disable_reopen

echo "Cleaning up..."
cd $RUN_DIR
rm -rf $INSTALL_DIR

echo "Installation completed!"
exec bash # update env