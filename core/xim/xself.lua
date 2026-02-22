-- Minimal self module: init / update / config / clean (no sudo, no shell config)
import("platform")

local function pconfig()
    return platform.get_config_info()
end

local function init()
    local cfg = pconfig()
    for _, d in ipairs({ cfg.homedir, cfg.rcachedir, cfg.bindir, cfg.libdir }) do
        if d and not os.isdir(d) then os.mkdir(d); cprint("[xlings:self]: created %s", d) end
    end
    cprint("[xlings:self]: init ok")
end

local function update()
    local cfg = pconfig()
    local homedir = cfg.homedir or cfg.install_dir
    if os.isdir(path.join(homedir, ".git")) then
        cprint("[xlings:self]: update from git ...")
        os.exec("git -C " .. homedir .. " pull")
    else
        cprint("[xlings:self]: %s is not a git repo, skip update", homedir)
    end
end

local function config()
    local cfg = pconfig()
    cprint("XLINGS_HOME: %s", cfg.homedir or "")
    cprint("XLINGS_DATA: %s", cfg.rcachedir or "")
    cprint("  bin: %s", cfg.bindir or "")
    cprint("  lib: %s", cfg.libdir or "")
end

local function clean()
    local cfg = pconfig()
    local cachedir = cfg.cachedir or path.join(cfg.homedir or "", ".xlings")
    if os.isdir(cachedir) then
        os.tryrm(cachedir)
        cprint("[xlings:self]: cleaned %s", cachedir)
    end
    cprint("[xlings:self]: clean ok")
end

local function help()
    cprint([[
xlings self [action]
  init    create home/data/bin/lib dirs
  update  git pull in XLINGS_HOME (if a repo)
  config  print paths
  clean   remove runtime cache
  help    this message
]])
end

function main(action, ...)
    action = action or "help"
    if action == "init" then init()
    elseif action == "update" then update()
    elseif action == "config" then config()
    elseif action == "clean" then clean()
    else help()
    end
end
