local xlings_install_dir = {
    linux = ".xlings",
    windows = "C:/Users/Public/xlings",
}

-- v0.4.40
local xlings_mdbook_url = {
    linux = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-unknown-linux-gnu.tar.gz",
    windows = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-pc-windows-msvc.zip",
}

local command_clear = {
    linux = "clear",
    windows = xlings_install_dir.windows .. "/tools/xlings_clear.bat",
}

local xlings_sourcedir = os.scriptdir() .. "/../"
local xlings_projectdir = "../"
local xlings_bookdir = xlings_projectdir .. "book/"
local xlings_drepodir = xlings_sourcedir .. "drepo/"

if os.host() == "linux" then
    xlings_install_dir.linux = os.getenv("HOME") .. "/" .. xlings_install_dir.linux
end

-- user config.xlings
local xlings_rundir -- Note: need init in xlings.lua
local xlings_editor

function set_rundir(rundir)
    xlings_rundir = rundir
end

function set_editor(editor)
    xlings_editor = editor
end

-- 

function get_config_info()
    return {
        install_dir = xlings_install_dir[os.host()],
        sourcedir = xlings_sourcedir,
        mdbook_url = xlings_mdbook_url[os.host()],
        cmd_clear = command_clear[os.host()],
        projectdir = xlings_projectdir,
        drepodir = xlings_drepodir,
        rundir = xlings_rundir,
        bookdir = xlings_bookdir,
        editor = xlings_editor,
    }
end

