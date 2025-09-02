import("core.base.json")

import("common")
import("platform")
import("base.log")
import("config.i18n")
import("base.utils")

local kv_cmds = {
    -- TODo: add kv cmds
    ["--xscript-info"] = false,  -- -search (string)
    ["--xscript-info-json"] = false,  -- -list (string)
}

-- TODO:
--   1.public use
-- xscript runtime - libxpkg
function main(script_file, ...)

    script_file = script_file or "main.lua"

    --print("script_file: " .. script_file)

    local script_file_path = script_file
    local _, cmds = common.xlings_input_process("", { ... }, kv_cmds)

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
                -- set runtime dir
                os.cd(platform.get_config_info().rundir)
                -- run script xpkg_main function
                script.xpkg_main(unpack(args))
            end
        end,
        catch {
            function (errors)
                --print("[xlings:xscript] main: error - ", errors)
                print("[private:xscript] main: error - ", errors)
                log.i18n_print(i18n.data()["common-qa-tips"])
                raise(errors)
            end
        }
    }
end