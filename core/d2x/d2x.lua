import("lib.detect.find_tool")

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
        [action .. "--template"] = false, -- -template (string)
        [action .. "--subpath"] = false, -- -template (string)
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

function user_commands_help(user_commands)
    if user_commands then
        cprint("${yellow bright}User Commands:")

        local user_commands_list = {}
        for cmd, _ in pairs(user_commands) do
            table.insert(user_commands_list, cmd)
        end

        table.sort(user_commands_list)

        for _, cmd in pairs(user_commands_list) do
            -- desc limit to 30 characters
            local desc = user_commands[cmd]

            if type(desc) == "table" then
                desc = desc[os.host()]
            end

            if desc then
                if #desc > 30 then
                    desc = desc:sub(1, 30) .. "..."
                end
                cprint("\t ${magenta}" .. cmd .. "${clear},      \t${cyan bright}" .. desc)
            end
        end

        cprint("")
    end
end

function user_commands_run(user_commands, action, args)
    
    cprint("[xligns:d2x]: ${yellow}run user command: ${cyan}" .. action)

    os.cd(platform.get_config_info().rundir)

    if type(user_commands[action]) == "table" then
        -- if user_commands[action] is a table, use the current host as the key
        user_commands[action] = user_commands[action][os.host()]
    end

    function __try_run(cmd)
        try {
            function() os.exec(cmd) end,
            catch {
                function(e)
                    print(e)
                    cprint("[xligns:d2x]: ${red}runtime error: ${cyan}" .. cmd)
                end
            }
        }
    end
    
    if (action == "build" or action == "run") and user_commands[action] then
        __try_run(string.format(user_commands[action] .. " " .. table.concat(args, " ")))
    else
        if user_commands[action] then
            __try_run(string.format(user_commands[action] .. " " .. table.concat(args, " ")))
        else
            cprint("[xligns:d2x]: ${red}command not found: ${cyan}" .. action)
        end
    end
end

function main(action, ...)
    local args = {...} or { "" }

    local main_target, cmds = _input_process(action, args)
    local d2x_config = platform.get_config_info().d2x or { }
    
    --print(d2x_config)
    --print("main_target: " .. main_target)
    --print(cmds)

    if action == "new" then
        if cmds["new--template"] then
            actions.templates(
                main_target,
                cmds["new--template"], cmds["new--subpath"]
            )
        else
            actions.init(main_target)
        end
    elseif action == "book" then
        local rundir = platform.get_config_info().rundir or os.curdir()
        local bookdir = path.join(rundir, "book")
        if os.isdir(bookdir) then
            if not find_tool("mdbook") then os.exec("xim mdbook -y") end
            os.iorun("mdbook serve --open " .. bookdir)
        else
            cprint("[xligns:d2x]: ${yellow}book not found.")
        end
    elseif action == "checker" then
        -- TODO: support to checker exercises directory, but haven't config file
        local d2x_config = platform.get_config_info().d2x
        if not cmds["checker--editor"] and d2x_config then
            cmds["checker--editor"] = d2x_config.checker.editor
        end
        actions.checker(main_target, { editor = cmds["checker--editor"] })
    elseif (action ~= nil and action ~= "") and d2x_config.commands then
        user_commands_run(d2x_config.commands, action, args)
    elseif action == "run" then
        actions.run(main_target)
    else
        log.i18n_print(i18n.data()["d2x"].help, action)
        user_commands_help(d2x_config.commands)
    end
end