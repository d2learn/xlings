# Xlings
export XLINGS_HOME="${XLINGS_HOME:-$HOME/.xlings}"
export XLINGS_DATA="$XLINGS_HOME/data"
export PATH="$XLINGS_HOME/subos/current/bin:$PATH"

# XVM
export XVM_WORKSPACE_NAME="global"

# Function to update the workspace name
xvm-workspace() {
    export XVM_WORKSPACE_NAME="$1"
}