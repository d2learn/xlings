import("core.base.text")
import("core.base.option")

import("platform")
import("common")
import("config")

import("xself")

local xinstall = import("xim.xim")
local xrun = import("xchecker.run")
local xchecker = import("xchecker.checker")

function xlings_help()
    cprint("${bright}xlings version:${clear} pre-v0.0.2")
    cprint("")
    cprint("${bright}Usage: $ ${cyan}xlings [command] [target]\n")

    cprint("${bright}Commands:${clear}")
    cprint("\t ${magenta}run${clear},      \t easy to run ${magenta}target${clear} - sourcecode file")
    cprint("\t ${magenta}install${clear},  \t install software/env(${magenta}target${clear})")
    cprint("\t ${magenta}self${clear},     \t self management")
    cprint("\t ${magenta}help${clear},     \t help info")
    cprint("")

    cprint("${bright}Project Commands:${clear} ${dim}(need config.xlings)${clear}")
    cprint("\t ${magenta}checker${clear},  \t start project's auto-exercises from ${magenta}target${clear}")
    cprint("")

    cprint("${bright}Short Commands:${clear} ${dim}(command alias)${clear}")
    cprint("\t ${green}xinstall/xim${clear}, \t xlings install")
    cprint("\t ${green}xrun${clear},     \t xlings run")
    cprint("")
    cprint("更多(More): ${underline}https://d2learn.org/xlings${clear}")
    cprint("")
end

function deps_check_and_install(xdeps)

    local xppcmds = nil
    local cmd_args = option.get("cmd_args") or { "-y" }
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
            xinstall("-i", name, "-y", "--disable-info", unpack(cmd_args))
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
    local cmd_args = option.get("cmd_args")

    -- config info - config.xlings
    local xname = option.get("xname")
    local xdeps = option.get("xdeps")
    local xchecker_config = option.get("xchecker_config")

    -- TODO: rename
    local xlings_lang = option.get("xlings_lang")
    local xlings_editor = option.get("xlings_editor")
    local xlings_runmode = option.get("xlings_runmode")

    -- init platform config
    platform.set_name(xname)
    platform.set_xchecker_config(xchecker_config)
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
    --print(xlings_lang)
    --print(xname)
    --print(xdeps)
    --print(cmd_args)

    if command ~= "self" and not os.isdir(platform.get_config_info().install_dir) then
        cprint("\n${yellow bright}>>>>>>>>[xlings init start]>>>>>>>>\n")
        xself.install()
        cprint("\n${yellow}--------------------------------------------\n")
        xself.init()
        cprint("\n${yellow bright}<<<<<<<<[xlings init end]<<<<<<<<\n")
    end

    -- TODO: optimize auto-deps install - xinstall(xx)
    if command == "checker" then
        if xlings_editor then xinstall(xlings_editor, "-y", "--disable-info") end
        xinstall(xlings_lang, "-y", "--disable-info")
        xchecker.main(cmd_target) -- TODO -s cmd_target
    elseif command == "run" then
        xrun(cmd_target)
    elseif command == "config" then
        config.llm()
    elseif command == "install" then
        if cmd_target then
            if cmd_args then
                xinstall(cmd_target, unpack(cmd_args))
            else
                xinstall(cmd_target)
            end
        elseif xdeps then
            deps_check_and_install(xdeps)
        else
            xinstall()
        end
    elseif command == "self" then
        xself(cmd_target)
    else
        xlings_help()
    end
end