import("core.base.text")
import("core.base.option")

import("platform")
import("common")
import("checker")
import("init")
import("drepo")
import("mini_run")
import("installer.xinstall")
import("config")

function xlings_help()
    cprint("${bright}xlings version:${clear} pre-v0.0.1")
    cprint("")
    cprint("${bright}Usage: $ ${cyan}xlings [command] [target]\n")

    cprint("${bright}Commands:${clear}")
    cprint("\t ${magenta}run${clear},      \t easy to run ${magenta}target${clear} - sourcecode file")
    cprint("\t ${magenta}install${clear},  \t install software/env(${magenta}target${clear})")
    cprint("\t ${magenta}drepo${clear},    \t print drepo info or download drepo(${magenta}target${clear})")
    cprint("\t ${magenta}update${clear},   \t update xlings to the latest version")
    cprint("\t ${magenta}uninstall${clear},\t uninstall xlings")
    cprint("\t ${magenta}help${clear},     \t help info")
    cprint("")

    cprint("${bright}Project Commands:${clear} ${dim}(need config.xlings)${clear}")
    cprint("\t ${magenta}init${clear},     \t init project by ${blue}config.xlings${clear}")
    cprint("\t ${magenta}book${clear},     \t open project's book in default browser")
    cprint("\t ${magenta}checker${clear},  \t start project's auto-exercises from ${magenta}target${clear}")
    cprint("")
    cprint("更多(More): ${underline}https://d2learn.org/xlings${clear}")
    cprint("")
end

function deps_check_and_install(xdeps)

    local xppcmds = nil

    -- project dependencies
    cprint("[xlings]: start deps check and install...")
    for name, value in pairs(xdeps) do
        if name == "xppcmds" then
            xppcmds = value
        else
            local pkg = {
                name = name,
                version = nil, -- TODO: support version
            }
            cprint("${dim}---${clear}")
            xinstall(name, {confirm = false, info = false, feedback = false})
        end
    end

--[[ -- TODO
    --cprint("[xlings]: deps check...")
    for name, _ in pairs(pkgs) do
        installed / not support ...
    end


    cprint("[xlings]: deps install...")
    for pkg, _ in pairs(pkgs) do
        xinstall(pkg.name, {confirm = false, info = false, feedback = false})
    end

    -- TODO: display pkg-name for not support or install failed
--]]

    if xppcmds then
        cprint("\n[xlings]: start run postprocess cmds...")
        os.cd(platform.get_config_info().rundir)
        for _, cmd in ipairs(xppcmds) do
            if type(cmd) == "table" then
                local host_os = common.get_linux_distribution().name

                if os.host() == "windows" or os.host() == "macosx" then
                    host_os = os.host()
                end

                if host_os == cmd[1] then
                    common.xlings_exec(cmd[2])
                else
                    -- TODO: support other platform
                    print("skip postprocess cmd: " .. cmd[2] .. " - " .. cmd[1])
                end
            else
                common.xlings_exec(cmd)
            end
        end
    end

end

function main()

    local run_dir = option.get("run_dir")
    local command = option.get("command")
    local cmd_target = option.get("cmd_target")

    -- config info - config.xlings
    local xname = option.get("xname")
    local xdeps = option.get("xdeps")

    -- TODO: rename
    local xlings_lang = option.get("xlings_lang")
    local xlings_editor = option.get("xlings_editor")
    local xlings_runmode = option.get("xlings_runmode")

    -- init platform config
    platform.set_name(xname)
    platform.set_lang(xlings_lang)
    platform.set_rundir(run_dir)
    platform.set_editor(xlings_editor)
    platform.set_runmode(xlings_runmode)

    -- llm config info - llm.config.xlings
    config.llm.load_global_config()
    local xlings_llm_config = option.get("xlings_llm_config")
    if xlings_llm_config then
        xlings_llm_config = common.xlings_config_file_parse(run_dir .. "/" .. xlings_llm_config)
        platform.set_llm_id(xlings_llm_config.id)
        platform.set_llm_key(xlings_llm_config.key)
        platform.set_llm_system_bg(xlings_llm_config.bg)
    end

    --print(run_dir)
    --print(command)
    --print(cmd_target)
    --print(xlings_name)
    --print(xlings_lang)
    --print(xname)
    --print(xdeps)

    -- TODO: optimize auto-deps install - xinstall(xx)
    if command == "checker" or command == xname then
        if xlings_editor then xinstall(xlings_editor, {confirm = false}) end
        xinstall(xlings_lang, {confirm = false, info = false, feedback = false})
        checker.main(cmd_target) -- TODO -s cmd_target
    elseif command == "run" then
        if os.isfile(path.join(run_dir, cmd_target)) then
            mini_run(cmd_target)
        else
            cprint("[xlings]: ${red}file not found${clear} - " .. cmd_target)
        end
    elseif command == "init" then
        xinstall("mdbook", {confirm = false})
        init.xlings_init(xname, xlings_lang)
    elseif command == "book" then
        --os.exec("mdbook build --open book") -- book is default folder
        xinstall("mdbook", {confirm = false})
        os.exec("mdbook serve --open " .. platform.get_config_info().bookdir) -- book is default folder
    elseif command == "update" then
        common.xlings_update(xname, xlings_lang)
        xlings_help()
    elseif command == "config" then
        config.llm()
    elseif command == "install" then
        if cmd_target == "xlings" then
            common.xlings_install() -- TODO: only for first install
        elseif cmd_target then
            xinstall(cmd_target, {confirm = true, info = true, feedback = true})
        elseif xdeps then
            deps_check_and_install(xdeps)
        else
            xinstall.list()
        end
    elseif command == "uninstall" then
        common.xlings_uninstall()
    elseif command == "drepo" then
        -- TODO drepo local-project management
        if cmd_target ~= "xlings_name" then
            drepo.print_drepo_info(cmd_target)
            cprint("[xlings]: try to download drepo - ${magenta}" .. cmd_target .. "${clear}")
            drepo.download_drepo(cmd_target)
        else
            cprint("\n\t${bright}drepo lists${clear}\n")
            drepo.print_all_drepo_info()
            cprint("\n\trun ${cyan}xlings drepo [drepo_name]${clear} to download\n")
        end
    else
        xlings_help()
    end
end