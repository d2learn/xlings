import("platform")
import("base.log")
import("config.i18n")

import("d2x.actions")

function _input_process(action, args)

    local main_target = ""

    action = tostring(action)
    args = args or {}

    local kv_cmds = {
        -- TODo: add kv cmds
        [action .. "-s"] = false,  -- -search (string)
        [action .. "--editor"] = false,  -- -list (string)
    }

    if #args > 0 and args[1]:sub(1, 1) ~= '-' then
        main_target = args[1]
    end

    for i = 1, #args do
        if kv_cmds[action .. args[i]] == false then
            kv_cmds[action .. args[i]] = args[i + 1] or ""
        end
    end

    return main_target, kv_cmds
end

function main(action, ...)
    local args = {...} or { "" }

    local main_target, cmds = _input_process(action, args)

    --print("main_target: " .. main_target)
    --print(cmds)

    if action == "new" then
        actions.init(main_target)
    elseif action == "book" then
        local rundir = platform.get_config_info().rundir or os.curdir()
        local bookdir = path.join(rundir, "book")
        if os.isdir(bookdir) then
            os.iorun("mdbook serve --open " .. bookdir)
        else
            cprint("[xligns:d2x]: ${yellow}book directory already exists.")
        end
    elseif action == "run" then
        actions.run(main_target)
    elseif action == "checker" then
        -- TODO: support to checker exercises directory, but haven't config file
        local d2x_config = platform.get_config_info().d2x
        if not cmds["checker--editor"] and d2x_config then
            cmds["checker--editor"] = d2x_config.checker.editor
        end
        actions.checker(main_target, { editor = cmds["checker--editor"] })
    else
        log.i18n_print(i18n.data()["d2x"].help, action)
    end
end