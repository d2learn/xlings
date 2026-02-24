import("config.xconfig")

local baseconfig = xconfig.load()

-- Path resolution: -P (os.projectdir) when xim root > XLINGS_HOME env > default ($HOME/.xlings)
local function detect_xlings_home()
    local proj = os.projectdir()
    if proj and proj ~= "" and os.isdir(proj) then
        if os.isdir(path.join(proj, "xim")) or os.isdir(path.join(proj, "core", "xim")) then
            return proj
        end
    end
    local env_home = os.getenv("XLINGS_HOME")
    if env_home and env_home ~= "" then
        return env_home
    end
    local user_home = os.getenv("HOME")
    if is_host("windows") then
        user_home = os.getenv("USERPROFILE")
    end
    return path.join(user_home or ".", ".xlings")
end

-- Global shared data: XLINGS_DATA env > default ($XLINGS_HOME/data)
local function detect_xlings_data(xlings_home)
    local env_data = os.getenv("XLINGS_DATA")
    if env_data and env_data ~= "" then
        return env_data
    end
    return path.join(xlings_home, "data")
end

-- Current subos: XLINGS_SUBOS env > default ($XLINGS_HOME/subos/default)
local function detect_xlings_subos(xlings_home)
    local env_subos = os.getenv("XLINGS_SUBOS")
    if env_subos and env_subos ~= "" then
        return env_subos
    end
    local active = baseconfig["activeSubos"] or "default"
    return path.join(xlings_home, "subos", active)
end

local xlings_home = detect_xlings_home()
local xlings_data = detect_xlings_data(xlings_home)
local xlings_subos = detect_xlings_subos(xlings_home)

local xlings_install_dir = xlings_home
local xlings_root_cache_dir = xlings_data

local xlings_bin_dir = path.join(xlings_subos, "bin")
local xlings_lib_dir = path.join(xlings_subos, "lib")
local xlings_subos_dir = xlings_subos

local command_clear = {
    linux = "clear",
    windows = xlings_install_dir .. "/tools/xlings_clear.bat",
    macosx = xlings_install_dir .. "/tools/xlings_clear.sh",
}

local command_wrapper = {
    linux = "",
    windows = xlings_install_dir .. "/tools/win_cmd_wrapper.bat ",
    macosx = ""
}

local xlings_sourcedir = path.directory(os.scriptdir())
local xlings_projectdir = xlings_sourcedir

local xname
local xlings_rundir
local xlings_lang
local xlings_cachedir = path.join(xlings_projectdir, ".xlings")
local xlings_runmode = "normal"

function set_name(name)
    xname = name
end

function set_lang(lang)
    xlings_lang = lang
end

function set_rundir(rundir)
    xlings_rundir = rundir
    xlings_cachedir = rundir .. "/.xlings/"
end

function set_runmode(runmode)
    xlings_runmode = runmode
end

function get_config_info()
    return {
        homedir = xlings_home,
        install_dir = xlings_install_dir,
        sourcedir = xlings_sourcedir,
        cmd_clear = command_clear[os.host()],
        cmd_wrapper = command_wrapper[os.host()],
        projectdir = xlings_projectdir,
        bindir = xlings_bin_dir,
        libdir = xlings_lib_dir,
        subosdir = xlings_subos_dir,
        rundir = xlings_rundir,
        rcachedir = xlings_root_cache_dir,
        cachedir = xlings_cachedir,
        name = xname,
        lang = xlings_lang,
        runmode = xlings_runmode,
    }
end
