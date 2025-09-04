import ("platform")

-- TODO: xvm?
import("xim.base.xvm")

-- for xpkg-template
function get_template_dir(template)

    -- namespace:pkgname@version
    local namespace
    local pkgname
    local version

    local parts = template:split("@")
    if #parts == 2 then
        version = parts[2]
    end
    parts = parts[1]:split(":")
    if #parts == 2 then
        namespace = parts[1]
        -- TODO: template xvm name? namespace-pkgname?
        pkgname = namespace .. "-" .. parts[2]
    else
        pkgname = parts[1]
    end

    if not xvm.has(pkgname) then
        cprint("[xlings]: template [ ${yellow}%s${clear} ] not found, try to install it...", template)
        ret = os.iorun(string.format("xim %s --disable-info -y", template))
        if ret and string.find(ret, "installed") then
            cprint("[xlings]: template [ ${green}%s${clear} ] installed", template)
        else
            cprint("[xlings]: ${red bright}failed to install template [%s]", template)
            return nil
        end
    end

    local info = xvm.info(pkgname, version or "")
    if info["SPath"] then
        return path.directory(info["SPath"])
    end

    return nil
end

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
        templatedir = get_template_dir(template)
    end

    if not os.isdir(templatedir) then
        cprint("[xlings]: ${red bright}template [%s] not found...", template)
    end

    if os.isfile(projectdir) or os.isdir(projectdir) then
        cprint("[xlings]: ${yellow bright}project dir [%s] already exists, please remove it first", target)
    else
        cprint("[xlings]: create project...")
        if os.trycp(templatedir, projectdir) then
            os.cd(projectdir)
            if os.isfile("config.xlings") then
                cprint("[xlings]: install project dependencies...")
                os.exec("xim")--os.exec("xim -y")
            end
            cprint("[xligns:d2x]: ${green}%s${clear} | ${yellow}%s${clear}", target, projectdir)
        else
            cprint("[xlings]: ${red bright}failed to create project${clear} from template [%s]", template)
        end
    end
end