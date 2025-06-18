import("devel.git")
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
        cprint("cloning %s...", git_url)
        git.clone(git_url)
        os.cd(name)
    else
        cprint("${bright}%s${clear} already exists, try to update...", name)
        os.cd(name)
        git.pull()
        git.clean({force = true})
    end

    -- 检查是否有 AUR 依赖
    local deps = string.trim(os.iorun("bash -c 'source PKGBUILD && echo -n ${depends[@]}'")):split(' ')
    local makedeps = string.trim(os.iorun("bash -c 'source PKGBUILD && echo -n ${makedepends[@]}'")):split(' ')

    for _, pkg in ipairs(deps) do if not is_pkg_in_pacman(pkg) then return false end end
    for _, pkg in ipairs(makedeps) do if not is_pkg_in_pacman(pkg) then return false end end

    -- 构建并安装包
    cprint("building %s...", name)
    os.exec("makepkg -si")

    return true
end

function is_pkg_in_pacman(pkg)
    if pacman.installed(pkg) or os.iorunv("pacman", {"-Si", pkg}) ~= nil then
        return true
    end
    cprint("${bright}%s${clear} not found in pacman", pkg)
    cprint("If you are a user then please install all AUR dependencies first, or notify the maintainer of this xpkg")
    cprint("Some dependencies may be located directly at the archlinuxcn source Try to check")
    cprint("If you are a maintainer then please add all them to the deps of xpkg and create xpkg for them")
    return false
end

function uninstall(name)
    return pacman.uninstall(name)
end

function info(name)

    local already_installed = try {
        function() return installed(name) end,
        catch = function() return false end
    }

    if already_installed then
        return pacman.info(name)
    end

    local info = aur_info(name)
    return format([[

    ${bright}[ XIM-AUR Package Info ]${clear}

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