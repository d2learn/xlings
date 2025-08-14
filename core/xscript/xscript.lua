import("core.base.json")

import("platform")
import("base.log")
import("config.i18n")
import("base.utils")

-- TODD: add input_args helper module, auto parse input args and help info
function _input_process(args)

    local main_target = ""

    action = tostring(action)
    args = args or {}

    local kv_cmds = {
        -- TODo: add kv cmds
        ["--xscript-info"] = false,  -- -search (string)
        ["--xscript-info-json"] = false,  -- -list (string)
    }

    if #args > 0 and args[1]:sub(1, 1) ~= '-' then
        main_target = args[1]
    end

    for i = 1, #args do
        if kv_cmds[args[i]] == false then
            kv_cmds[args[i]] = args[i + 1] or ""
        end
    end

    return main_target, kv_cmds
end

-- TODO:
--   1.public use
-- xscript runtime - libxpkg
function main(script_file, ...)

    script_file = script_file or "main.lua"

    --print("script_file: " .. script_file)

    local script_file_path = script_file
    local _, cmds = _input_process({ ... } or {})

    if os.isfile(script_file_path) then
        script_file_path = path.absolute(script_file_path)
    else
        --print("rundir:" .. platform.get_config_info().rundir)
        script_file_path = path.join(platform.get_config_info().rundir, script_file_path)
    end

    --print("script_file_path:" .. script_file_path)

    if not os.isfile(script_file_path) then
        --cprint("[private:xscript] ${red}main: script_file not found - " .. script_file)
        return
    end

    local args = { ... } or { "-h" }
    try {
        function ()
            local script = utils.load_module(
                script_file_path,
                path.directory(script_file_path)
            )
            if cmds["--xscript-info"] then
                print(script.package)
            elseif cmds["--xscript-info-json"] then
                -- { indent = true, indentdepth = 4 }
                print(json.encode(script.package))
            else
                -- run script xpkg_main function
                script.xpkg_main(unpack(args))
            end
        end,
        catch {
            function (errors)
                --print("[xlings:xscript] main: error - ", errors)
                print("[private:xscript] main: error - ", errors)
                log.i18n_print(i18n.data()["common-qa-tips"])
            end
        }
    }
end