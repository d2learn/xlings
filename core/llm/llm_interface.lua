import("lib.detect.find_tool")

import("common")
import("platform")

--[[ llm's interface
    api()
    generate_request_data(model, system_content, user_content)
    parse_response(content)
]]
import("llm.tongyi")


function get_llm()
    local llm_config = platform.get_config_info().llm_config
    local llm

    if llm_config.id == "tongyi" then
        llm = tongyi
    else
        print("[xlings]: llm not found or not supported")
        return
    end

    return llm
end

function generate_request_argvs(user_data)
    -- set basic arguments
    local argv = {}

    local llm_config = platform.get_config_info().llm_config

    -- ignore to check ssl certificates?
    --table.insert(argv, "-k")

    table.insert(argv, "--location")
    table.insert(argv, get_llm().api())

    table.insert(argv, "--max-time")
    table.insert(argv, "10")

    table.insert(argv, "--header")
    table.insert(argv, "Authorization: Bearer " .. llm_config.key)

    table.insert(argv, "--header")
    table.insert(argv, "Content-Type: application/json")

    table.insert(argv, "--data")
    local req_data = get_llm().generate_request_data("qwen-turbo", llm_config.system_bg, user_data)
    --cprint(llm_config.outputfile)
    common.xlings_create_file_and_write(llm_config.outputfile, req_data)
    -- user file avoid special characters in user_data
    table.insert(argv, "@" .. llm_config.outputfile)

    -- set outputfile
    table.insert(argv, "-o")
    table.insert(argv, llm_config.outputfile)

    return argv
end

function send_request(info)
    local tool = find_tool("curl")
    local llm_config = platform.get_config_info().llm_config
    if tool and llm_config.request_counter < 3000 then
        request_argv = generate_request_argvs(info)
        platform.set_llm_run_status(true)
        platform.set_llm_response("...")
        platform.add_llm_request_counter()
        --print("[xlings]: start run llm(req)...")
        os.vrunv(tool.program, request_argv)
        data = common.xlings_read_file(llm_config.outputfile)
        content = get_llm().parse_response(data)
        platform.set_llm_response(content)
        platform.set_llm_run_status(false)
    else
        cprint("[xlings]: curl not found or request counter over 3000")
    end
end