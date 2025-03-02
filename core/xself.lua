import("platform")
import("common")

local xinstall = import("xim.xim")

function install()

    uninstall() -- TODO: avoid delete mdbook?

    cprint("[xlings]: install xlings to %s", platform.get_config_info().install_dir)

    local install_dir = platform.get_config_info().install_dir

    -- cp xliings to install_dir(local xlings dir) and force overwrite
    os.cp(platform.get_config_info().sourcedir, install_dir, {force = true})

    -- create rcachedir
    local rcachedir = platform.get_config_info().rcachedir

    -- TODO: check cache data is valid
    if not os.isfile(path.join(rcachedir, "xlings.json")) then
        cprint("[xlings]: create rcachedir %s", rcachedir)
        os.mkdir(platform.get_config_info().rcachedir)

        -- create bin dir
        local bindir = platform.get_config_info().bindir
        cprint("[xlings]: create bindir %s", bindir)
        os.cp(path.join(install_dir, "bin"), bindir, {force = true})

        -- copy profile to rcachedir
        cprint("[xlings]: copy profile to rcachedir...")
        os.cp(path.join(install_dir, "config", "shell", "xlings-profile.*"), rcachedir, {force = true})

        -- copy xlings config file to rcachedir
        cprint("[xlings]: copy xlings config file to rcachedir...")
        os.cp(path.join(install_dir, "config", "xlings.json"), rcachedir, {force = true})
    else
        cprint("[xlings]: use local cache data - %s", rcachedir)
    end

    -- config user environment
    __config_environment(rcachedir)
end

function __config_environment(rcachedir)
    -- add bin to linux bashrc and windows's path env
    cprint("[xlings]: config user environment...")

    if is_host("linux") then
        local source_cmd_template = "\ntest -f %s && source %s"
        local posix_profile = path.join(rcachedir, "xlings-profile.sh")
        local fish_profile = path.join(rcachedir, "xlings-profile.fish")
        local shells = {
            bash = {
                profile = ".bashrc",
                command = format(source_cmd_template, posix_profile, posix_profile)
            },
            zsh = {
                profile = ".zshrc",
                command = format(source_cmd_template, posix_profile, posix_profile)
            },
            fish = {
                profile = ".config/fish/config.fish",
                command = format("\ntest -f %s; and source %s", fish_profile, fish_profile)
            }
        }

        function config_shell(shell)
            local profile = path.join(os.getenv("HOME"), shells[shell].profile)
            local command = shells[shell].command
            if os.isfile(profile) then
                cprint("[xlings]: config [%s] shell - %s", shell, profile)
                local profile_content = io.readfile(profile)
                if not string.find(profile_content, command, 1, true) then
                    common.xlings_file_append(profile, command)
                else
                    cprint("[xlings]: [%s] already configed", shell)
                end
            end
        end

        for shell, _ in pairs(shells) do
            config_shell(shell)
        end
    else
        local path_env = os.getenv("PATH")
        if not string.find(path_env, install_dir, 1, true) then

            path_env = path_env .. ";" .. install_dir
            -- os.setenv("PATH", path_env) -- only tmp, move to install.win.bat
            -- os.exec("setx PATH " .. path_env)
        end
    end
end

function init()
    cprint("[xlings]: init xlings...")
    os.addenv("PATH", platform.get_config_info().bindir)

    __config_environment(platform.get_config_info().rcachedir)

    common.xlings_exec([[xlings install xvm -y]])
    os.exec([[xvm add xim 0.0.2 --alias "xlings install"]])
    os.exec([[xvm add xinstall 0.0.2 --alias "xlings install"]])
    os.exec([[xvm add xrun 0.0.2 --alias "xlings run"]])
    os.exec([[xvm add xchecker 0.0.2 --alias "xlings checker"]])
    os.exec([[xvm add xself 0.0.2 --alias "xlings self"]])
    os.exec([[xvm add d2x 0.0.2 --alias "xlings d2x"]])
    os.exec([[xim --detect]])
    cprint("[xlings]: init xlings - ok")
end

function update()
    cprint("[xlings]: update xlings - todo")
end

function config()
    cprint("[xlings]: config xlings - todo")
end

function uninstall()
    local install_dir = platform.get_config_info().install_dir
    local rcachedir = platform.get_config_info().rcachedir
    try
    {
        function()
            os.rm(install_dir)
            cprint("[xlings]: remove %s - ok", install_dir)
            -- check rcachedir not empty by xlings.json
            if os.isfile(path.join(rcachedir, "xlings.json")) then
                cprintf("${cyan blink}-> ${clear}${yellow}delete local cache data?(y/n) ")
                io.stdout:flush()
                local confirm = io.read()
                if confirm == "y" then
                    cprint("[xlings]: remove %s - ok", rcachedir)
                    os.rm(rcachedir)
                end
            end
        end,
        catch
        {
            function (e)
                -- TODO: error: cannot remove directory C:\Users\Public\xlings Unknown Error (145)
                cprint("[xlings]: uninstall: " .. e)
            end
        }
    }

    cprint("[xlings]: xlings uninstalled - ok")

end

function help()
    -- xlings self [install|init|update|uninstall]
    -- please help me generate by copilot
    cprint("${bright}\t[ xlings self ]${clear} - xlings self sub-command")
    cprint("")
    cprint("${bright}Usage: $ ${cyan}xlings self [command]\n")
    cprint("${bright}Commands:${clear}")
    cprint("\t ${magenta}init${clear},     \t init xlings")
    cprint("\t ${magenta}update${clear},   \t update xlings to the latest version")
    cprint("\t ${magenta}uninstall${clear},\t uninstall xlings")
    cprint("\t ${magenta}help${clear},     \t help info")
    cprint("")
end

function main(action)
    if action == "enforce-install" then
        install()
    elseif action == "init" then
        init()
    elseif action == "config" then
        config()
    elseif action == "update" then
        update()
    elseif action == "uninstall" then
        uninstall()
    else
        help()
    end
end