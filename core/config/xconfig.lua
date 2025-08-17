import("core.base.json")

import("base.utils")

local xlings_config = {
    mirror = "GLOBAL",
    need_load_flag = true,
    xim = {
        ["index-repo"] = "https://github.com/d2learn/xim-pkgindex.git",
        ["index-repo-mirrors"] = { },
    }
}

local projectdir = path.directory(path.directory(os.scriptdir()))
local xlings_config_file = path.join(path.join(projectdir, "config"), "xlings.json")
-- Note: __xlings_homedir maybe not exist or just a tmp directory (in first install)
local __xlings_homedir = path.directory(projectdir)
local xlings_homedir_config_file = path.join(
    __xlings_homedir,
    ".xlings_data", "xlings.json"
)

if os.isfile(xlings_homedir_config_file) then
    --cprint("[xlings]: load config from ${yellow}" .. xlings_homedir_config_file)
    xlings_config_file = xlings_homedir_config_file
end

function load()
    if xlings_config.need_load_flag and os.isfile(xlings_config_file) then
        xlings_config = json.loadfile(xlings_config_file)
        -- when first install or need_update
        if xlings_config["need_update"] then
            local urls = {
                "https://github.com",
                "https://gitee.com",
            }
            local target_mirror = "CN"
            local ll_url = utils.low_latency_urls(urls)
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

            -- TODO: optmize json file format issue
            json.savefile(xlings_config_file, xlings_config)
        end
        xlings_config.need_load_flag = false
    else
        --cprint("${color.warning}xlings config file not found, use default config.")
    end
    return xlings_config
end

function save(config)
    if config and config["need_update"] ~= nil then
        json.savefile(xlings_config_file, config)
    else
        cprint("[xlings]: save config failed, config is nil.")
    end
end

function main()
    local config = load()
    print(config)
end