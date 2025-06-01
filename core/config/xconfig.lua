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

function load()
    local projectdir = path.directory(path.directory(os.scriptdir()))
    local xlings_config_file = path.join(path.join(projectdir, "config"), "xlings.json")
    if xlings_config.need_load_flag and os.isfile(xlings_config_file) then
        xlings_config = json.loadfile(xlings_config_file)
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

function main()
    local config = load()
    print(config)
end