import("devel.git")
import("privilege.sudo")

import("platform")
import("common")
import("base.utils")
import("base.log")
import("config.i18n")
import("config.xconfig")

local xinstall = import("xim.xim")

function install()

    uninstall() -- TODO: avoid delete mdbook?

    local pconfig = platform.get_config_info()

    if is_host("linux") or is_host("macosx") then
        -- create xlings home dir
        local xlings_homedir = pconfig.homedir
        cprint("[xlings]: create xlings home dir %s", xlings_homedir)
        local current_user = os.getenv("USER")
        sudo.exec("mkdir -p " .. xlings_homedir)
        sudo.exec(string.format("chown %s %s", current_user, xlings_homedir))
    elseif is_host("windows") then
        if os.isdir(pconfig.homedir) then
            cprint("[xlings]: xlings home dir %s already exists - [win]", pconfig.homedir)
        else
            cprint("[xlings]: create xlings home dir %s - [win]", pconfig.homedir)
            os.mkdir(pconfig.homedir)
        end
    end

    cprint("[xlings]: install xlings to %s", platform.get_config_info().install_dir)

    local install_dir = pconfig.install_dir

    -- cp xlings to install_dir(local xlings dir) and force overwrite
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
    --__config_environment(rcachedir)
end

function __config_environment(rcachedir)
    -- add bin to linux bashrc and windows's path env
    cprint("[xlings]: config user environment...")

    if is_host("linux") or is_host("macosx") then
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
                -- backup
                os.cp(profile, profile .. ".xlings.bak", {force = true})
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
        --local path_env = os.getenv("PATH")
        --if not string.find(path_env, installdir, 1, true) then

            --path_env = path_env .. ";" .. installdir
            -- os.setenv("PATH", path_env) -- only tmp, move to install.win.bat
            -- os.exec("setx PATH " .. path_env)
        --end
    end
end

function __xlings_usergroup_checker()
    cprint("[xlings]: check xlings user group(only unix)...")

    if is_host("linux") then
        local xlings_homedir = platform.get_config_info().homedir
        local current_user = os.getenv("USER")

        local exist_xlings_group = try {
            function()
                return os.iorun("getent group xlings")
            end,
            catch
            {
                function(e)
                    return nil
                end
            }
        }

        if not exist_xlings_group then
            -- only run once
            cprint("[xlings]: ${yellow dim}create group xlings and add current user [%s] to it(only-first)", current_user)
            sudo.exec("groupadd xlings")
            sudo.exec("usermod -aG xlings " .. current_user)
            cprint("[xlings]: ${yellow dim}add current %s owner to xlings group(only-first)", xlings_homedir)
            sudo.exec("chown -R :xlings " .. xlings_homedir)
            sudo.exec("chmod -R 775 " .. xlings_homedir)
            --sudo.exec("chmod g+x " .. xlings_homedir) -- directory's execute permission
            -- set gid, new file will inherit group
            sudo.exec("chmod -R g+s " .. xlings_homedir)
            -- set default acl, new file will inherit acl(group default rw)
            -- sudo.exec("setfacl -d -m g::rwx " .. xlings_homedir)
        elseif not string.find(os.iorun("groups " .. current_user), "xlings", 1, true) then
            cprint("")
            cprint("${yellow bright}Warning: current user [%s] is not in group xlings", current_user)
            cprint("")
            cprint("\t${cyan}sudo usermod -aG xlings %s${clear}", current_user)
            cprint("")
            --os.raise("please run command to add it")
            config({["config--adduser"] = current_user})
        else
            cprint("[xlings]: current user [%s] is in group xlings - ${green}ok", current_user)
        end

        -- TODO: optimize this issue
        -- set files permission
        local rcachedir = platform.get_config_info().rcachedir
        local files = {
            path.join(rcachedir, "xim", ".xim_fzf_out"),
            path.join(rcachedir, "xim", "xim-index-db.lua"),
            path.join(rcachedir, "xim", "xim-index-repos"),
            path.join(xlings_homedir, ".xmake"),
        }
        for _, file in ipairs(files) do
            if os.isfile(file) then
                sudo.exec("chmod g+rwx " .. file)
            elseif os.isdir(file) then
                sudo.exec("chmod -R g+rwx " .. file)
            end
            --cprint("[xlings]: ${dim}%s (permission) - ${green}ok", file)
        end

    end
end

function init()
    cprint("[xlings]: init xlings...")
    os.addenv("PATH", platform.get_config_info().bindir)

    local rcachedir = platform.get_config_info().rcachedir
    __config_environment(rcachedir)

    __xlings_usergroup_checker()

    common.xlings_exec([[xlings install xvm@0.0.5 -y]])
    os.exec([[xvm add xim 0.0.4 --alias "xlings install"]])
    os.exec([[xvm add xinstall 0.0.4 --alias "xlings install"]])
    os.exec([[xvm add xrun 0.0.4 --alias "xlings run"]])
    os.exec([[xvm add xchecker 0.0.4 --alias "xlings checker"]])
    os.exec([[xvm add xself 0.0.4 --alias "xlings self"]])
    os.exec([[xvm add d2x 0.0.4 --alias "xlings d2x"]])
    --os.exec([[xim --detect]])

    cprint("")
    cprint("\t${bright yellow}restart cmd/shell to update env")
    cprint("")

    cprint("[xlings]: init xlings - ok")
end

function update()
    local xlings_repo_default = "https://github.com/d2learn/xlings.git"
    local xlings_config = xconfig.load()
    local xlings_repo = xlings_config["repo"] or xlings_repo_default
    local xlings_latest_dir = path.join(platform.get_config_info().homedir, ".xlings_latest")

    if os.isdir(xlings_latest_dir) then
        cprint("[xlings]: remove old xlings latest dir %s", xlings_latest_dir)
        os.tryrm(xlings_latest_dir)
    end

    cprint("[xlings]: update xlings from ${green}%s${blink}...", xlings_repo)
    git.clone(xlings_repo, {
        depth = 1,
        outputdir = xlings_latest_dir,
    })

    cprint("[xlings]: replace old xlings files with latest...")
    -- action in xlings/xlings.bat
    -- step1: rm .xlings
    -- step2: mv .xlings_latest .xlings
    -- step3: xlings self init
end

function config(cmds)

    if cmds["config--mirror"] then
        -- TODO: add https:// support
    elseif cmds["config--res-server"] then
        local res_server = cmds["config--res-server"]
        -- TODO: support ftp/http protocol?
        if not string.find(res_server, "https", 1, true) then
            cprint("[xlings]: ${yellow}res-server must be a valid https url")
            return
        end
        local xlings_config = xconfig.load()
        xlings_config["xim"]["res-server"] = res_server
        xconfig.save(xlings_config)
        cprint("[xlings]: set res-server to ${yellow}%s${clear} - ${green}ok", res_server) 
    else
        local target_user = cmds["config--adduser"] or cmds["config--deluser"] or "unknown-user-flag"
        local user_included = string.find(os.iorun("groups " .. target_user), "xlings", 1, true)

        if not user_included and cmds["config--adduser"] then
            sudo.exec("usermod -aG xlings " .. cmds["config--adduser"])
            sudo.exec("chmod -R g+rwx " .. path.join(platform.get_config_info().homedir, ".xmake"))
            cprint("[xlings]: add user [%s] to group ${yellow}xlings${clear} - ${green}ok", cmds["config--adduser"])
        elseif cmds["config--deluser"] and user_included then
            sudo.exec("gpasswd -d " .. cmds["config--deluser"] .. " xlings")
            cprint("[xlings]: del user [%s] from group ${yellow}xlings${clear} - ${green}ok", cmds["config--deluser"])
        end
    end

end

function clean()
    os.tryrm(path.join(platform.get_config_info().install_dir, "core", ".xmake"))
    if is_host("linux") or is_host("macosx") then
        -- TODO: fix homedir issue for windows/linux
        os.tryrm(path.join(platform.get_config_info().homedir, ".xmake"))
    end
    os.iorun("xmake g -c")
    cprint("[xlings]: clean xlings tmp files - ${green}ok${clear}")
end

function uninstall()
    local install_dir = platform.get_config_info().install_dir
    local rcachedir = platform.get_config_info().rcachedir
    try
    {
        function()
            os.tryrm(install_dir)
            cprint("[xlings]: remove %s - ok", install_dir)
            -- check rcachedir not empty by xlings.json
            if os.isfile(path.join(rcachedir, "xlings.json")) then
                cprintf("${cyan blink}-> ${clear}${yellow}delete local cache data?(y/n) ")
                io.stdout:flush()
                local confirm = io.read()
                if confirm == "y" then
                    if is_host("linux") then
                        utils.remove_user_group_linux("xlings")
                        cprint("[xlings]: remove xlings user group - ${green}ok")
                        local current_user = os.getenv("USER")
                        sudo.exec("chown -R " .. current_user .. ":" .. current_user .. " " .. rcachedir)
                    elseif is_host("windows") then
                        cprint("[xlings]: try remove [D:/.xlings_data] ...")
                        os.tryrm("D:/.xlings_data")
                    end
                    if os.tryrm(rcachedir) then
                        cprint("[xlings]: remove %s - ${green}ok", rcachedir)
                    end
                else
                    cprint("[xlings]: keep local cache data: %s", rcachedir)
                end
            end
        end,
        catch
        {
            function (e)
                -- TODO: error: cannot remove directory C:\Users\Public\xlings\.xlings Unknown Error (145)
                cprint("[xlings]: uninstall: " .. e)
            end
        }
    }

    cprint("[xlings]: xlings uninstalled - ok")

end

function main(action, ...)

    action = action or ""

    local args = {...} or { "" }
    local kv_cmds = {
        -- config
        [action .. "--adduser"] = false,
        [action .. "--deluser"] = false,
        -- mirror
        [action .. "--mirror"] = false,
        [action .. "--res-server"] = false,
    }

    local _, cmds = common.xlings_input_process(action, args, kv_cmds)

    if action == "enforce-install" then
        install()
    elseif action == "init" then
        init()
    elseif action == "config" then
        config(cmds)
    elseif action == "clean" then
        clean()
    elseif action == "update" then
        update()
    elseif action == "uninstall" then
        uninstall()
    else
        log.i18n_print(i18n.data()["xself"].help, action)
    end
end