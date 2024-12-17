import("CmdProcessor")

function _input_process(args)

    local main_target = ""

    local boolean_cmds = {
        ["-y"] = false, -- -yes (boolean)
        ["--detect"] = false, -- -detect local installed software
        ["--disable-info"] = false, -- -feedback (boolean)
        ["--debug"] = false,  -- -debug (boolean)
    }

    -- Mutually Exclusive Commands 
    local main_cmds = {
        ["-i"] = false,  -- -install (string)
        ["-r"] = false,  -- -remove (string)
        ["-u"] = false,  -- -update (string)
        ["-s"] = false,  -- -search (string)
        ["-h"] = false   -- -help (string)
    }

    local kv_cmds = {
        -- TODo: add kv cmds
        ["-l"] = false,  -- -list (string)
        ["--update"] = false,  -- -update (string)
    }

    if #args > 0 and args[1]:sub(1, 1) ~= '-' then
        main_target = args[1]
    end

    for i = 1, #args do
        if boolean_cmds[args[i]] == false then
            boolean_cmds[args[i]] = true
        elseif main_cmds[args[i]] == false then
            main_cmds[args[i]] = true
            main_target = args[i + 1] or ""
        elseif kv_cmds[args[i]] == false then
            kv_cmds[args[i]] = args[i + 1] or ""
        end
    end

    -- "" is "true"
    if kv_cmds["-l"] and kv_cmds["-l"]:sub(1, 1) == '-' then
        kv_cmds["-l"] = ""
    end

    cmds = {
        install = main_cmds["-i"],
        remove = main_cmds["-r"],
        update = main_cmds["-u"],
        search = main_cmds["-s"],
        help = main_cmds["-h"],

        yes = boolean_cmds["-y"],
        sysdetect = boolean_cmds["--detect"],
        disable_info = boolean_cmds["--disable-info"],
        debug = boolean_cmds["--debug"],

        list = kv_cmds["-l"],
        sysupdate = kv_cmds["--update"],
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
