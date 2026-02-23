import("core.base.json")

import("base.netutils")

local xlings_config = {
    mirror = "GLOBAL",
    need_load_flag = true,
    xim = {
        ["index-repo"] = "https://github.com/d2learn/xim-pkgindex.git",
        ["index-repo-mirrors"] = { },
    }
}

-- Resolve XLINGS_HOME: env > default ($HOME/.xlings)
local function get_xlings_home()
    local env_home = os.getenv("XLINGS_HOME")
    if env_home and env_home ~= "" then
        return env_home
    end
    local user_home = os.getenv("HOME")
    if os.host() == "windows" then
        user_home = os.getenv("USERPROFILE")
    end
    return path.join(user_home or ".", ".xlings")
end

-- Resolve XLINGS_DATA: env > .xlings.json "data" > default ($XLINGS_HOME/data)
local function get_data_dir()
    local env_data = os.getenv("XLINGS_DATA")
    if env_data and env_data ~= "" then
        return env_data
    end
    local xlings_home = get_xlings_home()
    local dot_config = path.join(xlings_home, ".xlings.json")
    if os.isfile(dot_config) then
        local ok, cfg = pcall(json.loadfile, dot_config)
        if ok and cfg and cfg.data and cfg.data ~= "" then
            return cfg.data
        end
    end
    return path.join(xlings_home, "data")
end

-- Single config file: .xlings.json in XLINGS_HOME (holds data path, mirror, xim, repo, etc.)
local function get_primary_config_path()
    return path.join(get_xlings_home(), ".xlings.json")
end

local projectdir = path.directory(path.directory(os.scriptdir()))
local legacy_config = path.join(path.join(projectdir, "config"), "xlings.json")
local legacy_data_config = path.join(get_data_dir(), "xlings.json")

-- Resolve which config file to load: .xlings.json > config/xlings.json > data/xlings.json
local function resolve_config_file()
    local primary = get_primary_config_path()
    if os.isfile(primary) then
        return primary
    end
    if os.isfile(legacy_config) then
        return legacy_config
    end
    if os.isfile(legacy_data_config) then
        return legacy_data_config
    end
    return primary
end

local xlings_config_file = resolve_config_file()

function load()
    if xlings_config.need_load_flag then
        xlings_config_file = resolve_config_file()
        if os.isfile(xlings_config_file) then
            local defaults_xim = xlings_config.xim
            xlings_config = json.loadfile(xlings_config_file)
            if not xlings_config.xim then
                xlings_config.xim = defaults_xim
            end
            -- when first install or need_update
            if xlings_config["need_update"] then
                local urls = {
                    "https://github.com",
                    "https://gitee.com",
                }
                local target_mirror = "CN"
                local ll_url = netutils.low_latency_urls(urls)
                if string.find(ll_url, "github") then
                    cprint("[xlings]: mirror select -> ${yellow}[GLOBAL]")
                    target_mirror = "GLOBAL"
                else
                    cprint("[xlings]: mirror select -> ${yellow}[CN]")
                end

                xlings_config["xim"]["index-repo"] = xlings_config["xim"]["mirrors"]["index-repo"][target_mirror]
                xlings_config["xim"]["res-server"] = xlings_config["xim"]["mirrors"]["res-server"][target_mirror]

                if type(xlings_config["repo"]) == "table" then
                    xlings_config["repo"] = xlings_config["repo"][target_mirror]
                end
                xlings_config["need_update"] = false
                xlings_config["mirror"] = target_mirror

                json.savefile(get_primary_config_path(), xlings_config)
            end
        end
        xlings_config.need_load_flag = false
    end
    return xlings_config
end

-- Always save to .xlings.json (single config file)
function save(config)
    if config and config["need_update"] ~= nil then
        json.savefile(get_primary_config_path(), config)
    else
        cprint("[xlings]: save config failed, config is nil.")
    end
end

function main()
    local config = load()
    print(config)
end
