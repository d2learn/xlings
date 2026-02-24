# Xlings

if not set -q XLINGS_HOME
    set -x XLINGS_HOME "$HOME/.xlings"
end

set -x XLINGS_DATA "$XLINGS_HOME/data"
set -x PATH "$XLINGS_HOME/subos/current/bin" $PATH

# XVM
set -x XVM_WORKSPACE_NAME "global"

# Function to update the workspace name
function xvm-workspace
    set -x XVM_WORKSPACE_NAME $argv[1]
end