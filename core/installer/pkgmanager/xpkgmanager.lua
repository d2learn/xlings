--[[ interface - TODO
{
    install = install,
    uninstall = uninstall,
    update = update,
    upgrade = upgrade,
    search = search,
    list_installed = list_installed
}
]]

import("common")
import("installer.pkgmanager.apt")
import("installer.pkgmanager.winget")
import("installer.pkgmanager.pacman")

local xpm = nil

if os.host() == "windows" then
    xpm = winget
elseif os.host() == "linux" then
    local linux_host = common.get_linux_distribution()
    if linux_host.name == "ubuntu" or linux_host.name == "debian" then
        xpm = apt
    elseif linux_host.name == "arch linux" then
        -- TODO
    else
        cprint("[xlings] linux distribution not supported")
    end
elseif os.host() == "macosx" then
    -- TODO
end

pkg_name_map = {
    openjdk8 = {
        apt = "openjdk-8-jdk",
        winget = "AdoptOpenJDK.OpenJDK.8",
        pacman = "jdk8-openjdk",
    },
    dotnet = {
        apt = "dotnet-sdk-8.0",
        winget = "Microsoft.DotNet.SDK.8",
        pacman = "dotnet-sdk-8.0",
    }
}

function install(name)
    -- TODO: optimize
    name = pkg_name_map[name][xpm.info().name]
    if name then
        xpm.install(name)
    else
        cprint("[xlings] xpm pkg-map name not found")
    end
end

function uninstall(name)
    cprint("[xlings] uninstall not implemented")
end

function update()
    cprint("[xlings] update not implemented")
end

function upgrade(name)
    cprint("[xlings] upgrade not implemented")
end

function search(query)
    cprint("[xlings] search not implemented")
end

function list_installed()
    cprint("[xlings] list_installed not implemented")
end

function info()
    return xpm.info()
end

function get_xpm()
    local xpm = {
        install = install,
        uninstall = uninstall,
        update = update,
        upgrade = upgrade,
        search = search,
        list_installed = list_installed,
        info = info
    }
    return xpm
end

function main()
    local xpm = get_xpm()
    xpm.list_installed()
end