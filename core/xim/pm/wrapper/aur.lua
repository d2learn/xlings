import("core.base.json")

import("xim.base.runtime")
import("xim.pm.wrapper.pacman")

local aur_pkgs_dir = path.join(runtime.get_xim_data_dir(), "aur_pkgs")
local aur_url_template = "https://aur.archlinux.org/%s.git"
local aur_pkgs_info = {}

function installed(name)
    return pacman.installed(name)
end

function deps(name)
    local info = aur_info(name)
    -- table to string
    return info["Depends"]
end

function install(name)
    os.cd(aur_pkgs_dir)

    -- 克隆 AUR 仓库
    if not os.isdir(name) then
        local git_url = to_git_url(name)
        os.run("git clone %s", git_url)
        os.cd(name)
    else
        cpint("%s already exists, try to update...", name)
        os.cd(name)
        os.run("git pull")
    end

    -- 构建并安装包
    os.run("makepkg -si")

    return true
end

function uninstall(name)
    return pacman.uninstall(name)
end

function info(name)
    local info = aur_info(name)
    return format([[
    ${bright}[ XVM-AUR Package Info ]${clear}

Name: ${dim}%s${clear}
Version: ${dim}%s${clear}
License: ${dim}%s${clear}
Maintainer: ${dim}%s${clear}
URL: ${dim}%s${clear}
Popularity: ${dim}%s${clear}

    ${green bright}%s${clear}

        ]], info["Name"], info["Version"], table.concat(info["License"], ", "),
        info["Maintainer"], info["URL"], tostring(info["Popularity"]), info["Description"]
    )
end

function aur_info(name)
    -- check cache
    if aur_pkgs_info["results"] == nil or aur_pkgs_info["results"][1]["PackageBase"] ~= name then
        -- https://github.com/d2learn/xlings/pull/67#issuecomment-2580867360
        local curl_cmd = format([[curl -X 'GET' 'https://aur.archlinux.org/rpc/v5/info/%s' -H 'accept: application/json']], name)
        local data, _ = os.iorun(curl_cmd)
        if data then
            aur_pkgs_info = json.decode(data)
        else
            cprint("Failed to get AUR package info - %s", name)
        end
    end
    return aur_pkgs_info["results"][1]
end

function to_git_url(name)
    -- 假设输入形如 "https://aur.archlinux.org/<name>.git"
    return string.format(aur_url_template, name)
end

function main()
    local aur_git_url = "https://aur.archlinux.org/yay.git"
end