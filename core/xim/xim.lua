import("base.log")
import("config.i18n")

import("CmdProcessor")

function _input_process(args)

    local main_target = ""

    local boolean_cmds = {
        ["-y"] = false, -- -yes (boolean)
        ["--detect"] = false, -- -detect local installed software
        ["--disable-info"] = false, -- -feedback (boolean)
        ["-g"] = false,
        ["--global"] = false,
        ["--info-json"] = false, -- TODO: optimize this
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
        ["--update"] = false,  -- --update (string)
        ["--add-xpkg"] = false,  -- --add-xpkg (string)
        ["--add-indexrepo"] = false,  -- --add-indexrepo (string)
        ["--xpkg-args"] = false, -- --xpkg-args (string)
        ["--use"] = false, -- --use (string)
        ["--install-config-xlings"] = false, -- load legacy Lua config.xlings and install xim deps
    }

    -- When xlings passes "update --update index" / "search d2x" / "install -l" (after --), parse first word as command
    if #args > 0 then
        local first = args[1]
        if first == "install" then
            main_cmds["-i"] = true
            if args[2] == "-l" then
                kv_cmds["-l"] = ""
                main_target = ""
            else
                main_target = args[2] or ""
            end
        elseif first == "remove" then
            main_cmds["-r"] = true
            main_target = args[2] or ""
        elseif first == "update" then
            main_cmds["-u"] = true
            if args[2] == "--update" then
                kv_cmds["--update"] = args[3] or ""
                main_target = ""
            else
                main_target = args[2] or ""
            end
        elseif first == "search" then
            main_cmds["-s"] = true
            main_target = args[2] or ""
        elseif first:sub(1, 1) ~= '-' then
            main_target = first
        end
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
        enable_global = boolean_cmds["--global"] or boolean_cmds["-g"],

        list = kv_cmds["-l"],
        sysupdate = kv_cmds["--update"],
        sysadd_xpkg = kv_cmds["--add-xpkg"],
        sysadd_indexrepo = kv_cmds["--add-indexrepo"],
        sysxpkg_args = kv_cmds["--xpkg-args"],
        sysxim_args = args, -- is table, xim's original args

        info_json = boolean_cmds["--info-json"],
        sys_use = kv_cmds["--use"],
        install_config_xlings = kv_cmds["--install-config-xlings"],
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
                log.i18n_print(i18n.data()["common-qa-tips"])
                raise(errors)
            end
        }
    }
end
