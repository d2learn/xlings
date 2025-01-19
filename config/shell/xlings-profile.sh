# Xlings
export XLINGS_HOME="$HOME/.xlings"
export XLINGS_DATA="$HOME/.xlings_data"
export XLINGS_BIN="$HOME/.xlings_data/bin"
export PATH="$XLINGS_BIN:$PATH"

# XVM
export XVM_WORKSPACE_NAME="global"

# Function to update the workspace name
xvm-workspace() {
    export XVM_WORKSPACE_NAME="$1"
}