import ("platform")

function main(target, template)

    if target == nil or target == "" then
        target = template
    end

    if template == nil or template == "" then
        cprint("[xlings]: ${red}--template [name] is required${clear}")
        return
    end

    cprintf("[xligns]: checking templates cache... ")
    io.stdout:flush()

    local ret = os.iorun("xim xlings-project-templates --disable-info -y")

    if ret and string.find(ret, "installed") then
        cprint("- ${green}ok")
    else
        cprint("- ${red}failed")
        cprint([[

    ${yellow bright}Try Reinstall Templates Module${clear}

$ ${cyan}xlings remove xlings-project-templates${clear}
$ ${cyan}xlings install xlings-project-templates

        ]])
        return
    end

    local config = platform.get_config_info()
    local templates_rootdir = path.join(
        config.rcachedir,
        "xim", "xpkgs", "xlings-project-templates", "latest"
    )
    local templates = template:split("-")
    local templatedir = path.join(templates_rootdir, table.concat(templates, "/"))
    local projectdir = path.join(config.rundir, target)

    if not os.isdir(templatedir) then
        cprint("[xlings]: ${red bright}template [%s] not found...", template)
    else
        if os.isfile(projectdir) or os.isdir(projectdir) then
            cprint("[xlings]: ${yellow bright}project dir [%s] already exists, please remove it first", target)
        else
            cprint("[xlings]: create project...")
            if os.trycp(templatedir, projectdir) then
                cprint("[xlings]: install project dependencies...")
                os.cd(projectdir)
                os.exec("xim")--os.exec("xim -y")
                cprint("[xligns:d2x]: ${green}%s${clear} | ${yellow}%s${clear}", target, projectdir)
            else
                cprint("[xlings]: ${red bright}failed to create project${clear} from template [%s]", template)
            end
        end
    end
end