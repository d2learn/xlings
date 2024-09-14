import("common")
import("platform")

local llm_global_config_file = path.join(platform.get_config_info().rcachedir, "config.llm.xlings")

function load_global_config()
    if os.isfile(llm_global_config_file) then
        llm_config = common.xlings_config_file_parse(llm_global_config_file)
        platform.set_llm_id(llm_config.id)
        platform.set_llm_key(llm_config.key)
        platform.set_llm_system_bg(llm_config.bg)
    end
end

function save_global_config(config)
    common.xlings_save_config_to_file(config, llm_global_config_file)
    cprint("[xlings]: ${bright}llm global config saved${clear}")
end

function main()
    -- please input y or n
    cprint("[xlings]: start llm global config...")

    local llm_config = {}

    io.write("xlings_llm_id: ")
    local xlings_llm_id = io.read()
    llm_config.xlings_llm_id = xlings_llm_id

    io.write("xlings_llm_key: ")
    local xlings_llm_key = io.read()
    llm_config.xlings_llm_key = xlings_llm_key

    io.write("xlings_llm_system_bg: ")
    local xlings_llm_system_bg = io.read()
    llm_config.xlings_llm_system_bg = xlings_llm_system_bg

    save_global_config(llm_config)
end