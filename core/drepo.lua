import("common")
import("platform")

function drepo_path_format(drepo_name)
    return platform.get_config_info().drepodir .. drepo_name .. ".drepo"
end

function read_drepo_info(drepo_file)
    local drepo_info = {}
    local drepo_file = io.open(drepo_file, "r")
    if drepo_file then
        for line in drepo_file:lines() do
            local key, value = line:match("([^=]+)=(.+)")
            if key and value then
                drepo_info[key:trim()] = value:trim()
            end
        end
        drepo_file:close()
    end
    return drepo_info
end

function print_drepo_info(drepo_name, drepo_file)
    if not drepo_file then
        drepo_file = drepo_path_format(drepo_name)
    end
    local drepo_info = read_drepo_info(drepo_file)
    for key, value in pairs(drepo_info) do
        print(key .. "\t: " .. value)
    end
end

function print_all_drepo_info()
    -- support linux and windows
    local drepo_dir = platform.get_config_info().drepodir
    for _, drepo_file in ipairs(os.files(drepo_dir .. "*.drepo")) do
        drepo_name = path.filename(drepo_file):match("(.+).drepo")
        cprint("--- ${magenta}" .. drepo_name .. "${clear} ---")
        print_drepo_info(nil, drepo_file)
    end
end

function download_drepo(drepo_name)
    local drepo_file = drepo_path_format(drepo_name)
    local drepo_git = read_drepo_info(drepo_file).git
    common.xlings_download_repo(drepo_git, platform.get_config_info().rundir)
end