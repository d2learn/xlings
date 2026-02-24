# Xlings Shell Profile (bash/zsh)

_xlings_dir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." 2>/dev/null && pwd)"
if [ -n "$_xlings_dir" ]; then
    export XLINGS_HOME="$_xlings_dir"
fi
unset _xlings_dir

export XLINGS_BIN="$XLINGS_HOME/subos/current/bin"

case ":$PATH:" in
    *":$XLINGS_BIN:"*) ;;
    *) export PATH="$XLINGS_BIN:$XLINGS_HOME/bin:$PATH" ;;
esac