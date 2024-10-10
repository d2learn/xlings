#!/bin/bash

# avoid sub-script side effects
QI_RUN_DIR=`pwd`
QI_INSTALL_DIR=".xlings_software_install"

SOFTWARE_URL="https://github.com/d2learn/xlings/archive/refs/heads/main.zip"
ZIP_FILE="software.zip"
XLINGS_DIR="xlings-main"
INSTALL_SCRIPT="tools/install.unix.sh"

# ------------------------------

echo -e "$(cat << 'EOF'

 __   __  _      _                     
 \ \ / / | |    (_)    pre-v0.0.1                
  \ V /  | |     _  _ __    __ _  ___ 
   > <   | |    | || '_ \  / _` |/ __|
  / . \  | |____| || | | || (_| |\__ \
 /_/ \_\ |______|_||_| |_| \__, ||___/
                            __/ |     
                           |___/      

repo:  https://github.com/d2learn/xlings
forum: https://forum.d2learn.org

---

EOF
)"


SOFTWARE_URL1="https://github.com/d2learn/xlings/archive/refs/heads/main.zip"
SOFTWARE_URL2="https://gitee.com/sunrisepeak/xlings-pkg/raw/master/xlings-main.zip"

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

measure_latency() {
    local url="$1"
    local domain=$(echo "$url" | sed -e 's|^[^/]*//||' -e 's|/.*$||')
    local latency

    if command_exists ping; then
        latency=$(ping -c 3 -q "$domain" 2>/dev/null | awk -F'/' 'END{print $5}')
    elif command_exists curl; then
        latency=$(curl -o /dev/null -s -w '%{time_total}\n' "$url")
    else
        echo "Error: Neither ping nor curl is available." >&2
        exit 1
    fi

    if [ -n "$latency" ]; then
        echo "$latency"
    else
        echo "999999"
    fi
}

echo "Testing network..."
latency1=$(measure_latency "$SOFTWARE_URL1")
latency2=$(measure_latency "$SOFTWARE_URL2")

echo "Latency for github.com : $latency1 ms"
echo "Latency for gitee.com : $latency2 ms"

# 比较延迟并选择最快的 URL
if command_exists bc; then
    if (( $(echo "$latency1 < $latency2" | bc -l) )); then
        SOFTWARE_URL=$SOFTWARE_URL1
    else
        SOFTWARE_URL=$SOFTWARE_URL2
    fi
else
    # 如果 bc 不可用，使用简单的字符串比较
    if [[ "$latency1" < "$latency2" ]]; then
        SOFTWARE_URL=$SOFTWARE_URL1
    else
        SOFTWARE_URL=$SOFTWARE_URL2
    fi
fi

# ------------------------------

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
if [ -d "$QI_INSTALL_DIR" ]; then
    rm -rf "$QI_INSTALL_DIR"
fi

# create tmp dir
mkdir -p "$QI_INSTALL_DIR"
cd "$QI_INSTALL_DIR"

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
cd $QI_RUN_DIR
echo "Removing $QI_RUN_DIR/$QI_INSTALL_DIR(tmpfiles)..."
rm -rf $QI_INSTALL_DIR

echo "Installation completed!"
exec bash # update env