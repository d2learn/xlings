# Xlings
set -x XLINGS_HOME "$HOME/.xlings"
set -x XLINGS_DATA "$HOME/.xlings_data"
set -x XLINGS_BIN "$HOME/.xlings_data/bin"
set -x PATH "$XLINGS_BIN" $PATH

# XVM
set -x XVM_WORKSPACE_NAME "global"

# Function to update the workspace name
function xvm-workspace
    set -x XVM_WORKSPACE_NAME $argv[1]
end