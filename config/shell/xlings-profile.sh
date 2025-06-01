# Xlings
export XLINGS_HOME="/home/xlings/.xlings"

if [ "$(uname)" = "Darwin" ]; then
    export XLINGS_HOME="/Users/xlings"
fi

export XLINGS_DATA="$XLINGS_HOME/.xlings_data"
export XLINGS_BIN="$XLINGS_HOME/.xlings_data/bin"
export PATH="$XLINGS_BIN:$PATH"

# XVM
export XVM_WORKSPACE_NAME="global"

# Function to update the workspace name
xvm-workspace() {
    export XVM_WORKSPACE_NAME="$1"
}