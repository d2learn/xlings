import("core.base.json")

local __data = nil

function data()
    --local config = platform.get_config_info()
    local local_lang = "en" -- Default language

    if is_host("linux") then
        local tmp_local_lang = os.getenv("LANG") or "en"
        if tmp_local_lang:find("zh") then
            local_lang = "zh"
        end
    end

    local configdir = path.directory(path.directory(os.scriptdir()))
    local i18n_config_file = path.join(configdir, "config", "i18n", local_lang .. ".json")
    if __data == nil then
        if os.isfile(i18n_config_file) then
            __data = json.loadfile(i18n_config_file)
        else
            cprint("${error}i18n config file not found - %s", i18n_config_file)
        end
    end

    return __data
end

function main()
    load()
    import("base.log")
    log.i18n_print(__data.help, "World")
end