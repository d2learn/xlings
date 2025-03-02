# Xlings
export XLINGS_HOME="/home/xlings/.xlings"
export XLINGS_DATA="/home/xlings/.xlings_data"
export XLINGS_BIN="/home/xlings/.xlings_data/bin"
export PATH="$XLINGS_BIN:$PATH"

# XVM
export XVM_WORKSPACE_NAME="global"

# Function to update the workspace name
xvm-workspace() {
    export XVM_WORKSPACE_NAME="$1"
}