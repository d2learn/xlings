import("core.base.text")
import("core.base.option")

import("platform")
import("common")
import("config")

import("xself")

local xinstall = import("xim.xim")
local d2x = import("d2x.d2x")

function xlings_help()
    cprint("${bright}xlings version:${clear} pre-v0.0.2")
    cprint("")
    cprint("${bright}Usage: $ ${cyan}xlings [command] [target]\n")

    cprint("${bright}Commands:${clear}")
    cprint("\t ${magenta}new${clear},      \t create project template")
    cprint("\t ${magenta}install${clear},  \t install packages or project dependencies")
    cprint("\t ${magenta}use${clear},      \t swtich target version")
    cprint("\t ${magenta}remove${clear},   \t remove packages")
    cprint("\t ${magenta}checker${clear},  \t run project's exercise checker")
    cprint("\t ${magenta}self${clear},     \t self management")
    cprint("\t ${magenta}help${clear},     \t help info")
    cprint("")

    cprint("${bright}Tools:${clear}")
    cprint("\t ${green}xim${clear},     \t xlings installation manager")
    cprint("\t ${green}xvm${clear},     \t xlings version manager")
    cprint("\t ${green}d2x${clear},     \t xlings d2x project tool")
    cprint("\t ${green}xself${clear},   \t xlings self management")
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

function _submodule_call(command, action, cmd_args)
    if cmd_args then
        command(action, unpack(cmd_args))
    else
        command(action)
    end
end

function _command_dispatch(command, action, target, cmd_args)
    local args = cmd_args or {}
    if target then
        table.insert(args, 1, target)
    end
    _submodule_call(command, action, args)
end

function xlings_xvm_use(cmd_target, cmd_args)
    try {
        function()
            if cmd_target and cmd_args then
                os.execv("xvm", { "use", cmd_target, unpack(cmd_args) })
            elseif cmd_target then
                os.execv("xvm", { "list", cmd_target })
            else
                cprint("[xlings]: ${red}switch version failed...")
                cprint("")
                cprint("\t${cyan bright}xlings use [target] [version] ${clear}")
                cprint("")
            end
        end,
        catch {
            function()
                os.execv("xvm", { "--help" })
            end
        }
    }
end

function recall_if(rundir, command, cmd_target, args)
    local config_file = path.join(rundir, "config.xlings")
    if os.isfile(config_file) then

        local local_config_dir = path.join(rundir, ".xlings")
        local need_recall = false

        if os.projectdir() ~= local_config_dir then
            cmd_target = cmd_target or ""
            if command == "checker" then need_recall = true
            elseif command == "install" and cmd_target == "" then need_recall = true
            elseif command == "d2x" and cmd_target == "checker" then need_recall = true
            end
        end

        --print("command: " .. command)
        --print("need_recall: " .. tostring(need_recall))
        --print([[%s - %s]], os.projectdir(), local_config_dir)

        if need_recall then
            -- 0.generate xlings config file and verify
            if not os.isdir(local_config_dir) then os.mkdir(local_config_dir) end
            local tmp_file = path.join(local_config_dir, "config-xlings.lua")
            os.tryrm(tmp_file)
            os.cp(config_file, tmp_file)
            try { -- TODO: config file verify
                function ()
                    import("config-xlings", {rootdir = local_config_dir})
                end,
                catch {
                    function (e)
                        cprint("[xlings]: ${yellow}load local ${red bright}config.xlings${clear} ${yellow}failed...")
                        print("\n" .. tostring(e) .. "\n")
                        os.raise("please check: [ %s ]", config_file)
                    end
                }
            }

            -- 1. copy project file (xmake)
            local xmake_template_file = path.join(
                platform.get_config_info().install_dir,
                "tools",
                "template." .. os.host() .. ".xlings"
            )
            local xmake_file = path.join(local_config_dir, "xmake.lua")

            os.tryrm(xmake_file)
            os.cp(xmake_template_file, xmake_file)

            -- 2. recall xlings in local config dir
            local xlings_args = args or {}
            if cmd_target ~= "" then table.insert(xlings_args, 1, cmd_target) end
            table.insert(xlings_args, 1, command)
            os.cd(local_config_dir)
            os.execv("xmake", { "xlings", rundir, unpack(xlings_args) })
            os.exit()
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
    local xdeps = option.get("xdeps") -- TODO: deprecated, use xim
    if option.get("xim_deps") then
        xdeps = option.get("xim_deps")
    end
    local d2x_config = option.get("d2x_config")

    -- TODO: rename
    local xlings_lang = option.get("xlings_lang")
    local xlings_runmode = option.get("xlings_runmode")

    -- init platform config
    platform.set_name(xname)
    platform.set_d2x_config(d2x_config)
    platform.set_lang(xlings_lang)
    platform.set_rundir(run_dir)
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

    -- TODO: optimize
    recall_if(run_dir, command, cmd_target, cmd_args)

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
    if command == "new" then
        _command_dispatch(d2x, "new", cmd_target, cmd_args)
    elseif command == "install" then
        -- TODO: only support install, and move deps to xinstall/xim
        if cmd_target then
            _submodule_call(xinstall, cmd_target, cmd_args)
        elseif xdeps then
            deps_check_and_install(xdeps)
        else
            xinstall()
        end
    elseif command == "use" then
        xlings_xvm_use(cmd_target, cmd_args)
    elseif command == "checker" then
        _command_dispatch(d2x, "checker", cmd_target, cmd_args)
    elseif command == "remove" then
        _command_dispatch(xinstall, "-r", cmd_target, cmd_args)
    elseif command == "config" then
        config.llm()
    else -- submodule
        local submodule_map = {
            ["d2x"] = d2x,
            --["im"] = xinstall,
            ["self"] = xself,
        }

        if submodule_map[command] then
            _submodule_call(submodule_map[command], cmd_target, cmd_args)
        else
            xlings_help()
        end
        
    end
end