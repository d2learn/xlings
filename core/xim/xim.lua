import("CmdProcessor")

function _input_process(args)

    if #args == 1 and args[1]:sub(1, 1) ~= '-' then
        return args[1], { install = true }
    end

    local main_target = ""

    local boolean_cmds = {
        ["-y"] = false, -- -yes (boolean)

        -- double dash commands
        ["--yes"] = false, -- -yes (boolean)
        ["--detect"] = false, -- -detect local installed software
    }

    -- Mutually Exclusive Commands 
    local main_cmds = {
        ["-i"] = false,  -- -install (string)
        ["-r"] = false,  -- -remove (string)
        ["-u"] = false,  -- -update (string)
        ["-l"] = false,  -- -list (string)
        ["-s"] = false,  -- -search (string)
        ["-h"] = false   -- -help (string)
    }

    local kv_cmds = {
        -- TODo: add kv cmds
    }

    for i = 1, #args do
        if boolean_cmds[args[i]] == false then
            boolean_cmds[args[i]] = true
        elseif main_cmds[args[i]] == false then
            main_cmds[args[i]] = true
            main_target = args[i + 1]
        end
    end

    cmds = {
        install = main_cmds["-i"],
        remove = main_cmds["-r"],
        update = main_cmds["-u"],
        list = main_cmds["-l"],
        search = main_cmds["-s"],
        help = main_cmds["-h"],
        yes = boolean_cmds["-y"] or boolean_cmds["--yes"],
        detect = boolean_cmds["--detect"]
    }

    return main_target, cmds
end

function _tests()
    CmdProcessor.new("vscode", {"-i"}):run()
end

-- xim [command] [target]
-- command: -i -r -u -l -s -h -y
-- target name@version@maintainer
-- example:
--      xmake l xim.lua -h
--      xmake l xim.lua vscode
--      xmake l xim.lua -i vscode
--      xmake l xim.lua -r vscode -y
function main(...)
    local args = { ... } or { "-h" }
    try {
        function ()
            local main_target, cmds = _input_process(args)
            --print("main_target: ", main_target)
            --print("cmds: ", cmds)
            CmdProcessor.new(main_target, cmds):run()
        end,
        catch {
            function (errors)
                print("[xlings:xim] main: error - ", errors)
            end
        }
    }
end
