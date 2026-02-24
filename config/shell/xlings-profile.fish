# Xlings

if test (uname) = "Darwin"
    set -x XLINGS_HOME "/Users/xlings"
else
    set -x XLINGS_HOME "/home/xlings"
end

set -x XLINGS_DATA "$XLINGS_HOME/data"
set -x XLINGS_BIN "$XLINGS_HOME/data/bin"
set -x PATH "$XLINGS_BIN" $PATH

# XVM
set -x XVM_WORKSPACE_NAME "global"

# Function to update the workspace name
function xvm-workspace
    set -x XVM_WORKSPACE_NAME $argv[1]
end