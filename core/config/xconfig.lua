import("core.base.json")

import("platform")
import("base.utils")

local xlings_config = {
    need_load_flag = true,
    xim = {
        ["index-repo"] = "https://github.com/d2learn/xim-pkgindex.git",
        ["index-repo-mirrors"] = { },
    }
}

function load()
    local config = platform.get_config_info()
    local xlings_config_file = path.join(config.rcachedir, "xlings.json")
    if xlings_config.need_load_flag and os.isfile(xlings_config_file) then
        xlings_config = json.loadfile(xlings_config_file)
        if xlings_config["xim"]["index-repo"] == "xconfig-placeholder" then
            local urls = xlings_config["xim"]["index-repo-mirrors"]
            local xim_default_index = utils.low_latency_urls(urls)
            cprint("${green dim}[xlings]: set xim default index repo: " .. xim_default_index)
            xlings_config["xim"]["index-repo"] = xim_default_index
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