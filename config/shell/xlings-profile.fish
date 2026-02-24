# Xlings Shell Profile (fish)

set -l _script_dir (dirname (status filename))
set -gx XLINGS_HOME (dirname (dirname "$_script_dir"))

set -gx XLINGS_BIN "$XLINGS_HOME/subos/current/bin"

if not contains "$XLINGS_BIN" $PATH
    set -gx PATH "$XLINGS_BIN" "$XLINGS_HOME/bin" $PATH
end