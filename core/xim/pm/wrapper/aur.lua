import("devel.git")
import("core.base.json")

import("xim.base.runtime")
import("xim.base.utils")
import("xim.pm.wrapper.pacman")

local aur_pkgs_dir = path.join(runtime.get_xim_data_dir(), "aur_pkgs")
local aur_url_template = "https://aur.archlinux.org/%s.git"
local aur_pkgs_info = {}

local aur_helpers = { "yay", "paru" }

function installed(name)
    return pacman.installed(name)
end

function deps(name)
    local info = aur_info(name)
    -- table to string
    return info["Depends"]
end

function install(name)
    if install_via_helper(name) then return true end
    cprint("${yellow}No AUR Helper found, try install with makepkg")
    return install_via_makepkg(name)
end

function install_via_helper(name, arguments)
    local arguments = arguments or {}
    table.insert(arguments, "-S")
    table.insert(arguments, name)

    for _, aur_helper in ipairs(aur_helpers) do
        if os.exists("/usr/bin/" .. aur_helper) then
            cprint("AUR Helper ${bright}%s${clear} detected, try to install with it", aur_helper)
            local ok = os.execv(aur_helper, arguments)
            return ok == 0
        end
    end
    return false
end

function install_via_makepkg(name)
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
    -- 此处既然已有 PKGBUILD 就不必网络获取
    local deps = string.trim(os.iorun("bash -c 'source PKGBUILD && echo -n ${depends[@]} ${makedepends[@]}'")):split(' ')
    for _, pkg in ipairs(deps) do if not is_pkg_installed_or_in_pacman(pkg) then return try_install_aur_helper(pkg) end end

    -- 构建并安装包
    cprint("building %s...", name)
    os.exec("makepkg -si")

    os.cd("-")

    return true
end

function is_pkg_installed_or_in_pacman(pkg)
    -- 提取包名
    -- 软件包名称只能包含字母数字字符以及 @、.、_、+、- 中的任何字符。名称不允许以连字符或点开头。所有字母都应为小写。
    pkg = pkg:match("^[a-z0-9@_+][a-z0-9@._+-]*")
    -- 判断   是否已安装   或   在 pacman 中有
    if _try_pacman_installed(pkg) or _try_info_pacman(pkg) then return true end

    -- 提示应将非官方源的依赖包添加至 xpkg 以处理依赖
    cprint([[
              ${dim cyan bright}%s${clear bright yellow} not found in pacman${clear}
${yellow}
If you are a user, install all dependencies first,
or install any AUR Helper such as yay or paru,
or notify the maintainer of this xpkg.

Some dependencies may be located in the archlinuxcn repo.

If you are a maintainer, add all dependencies that are
not in the official repos to the deps of the xpkg,
and create the xpkg for them.
${clear}
    ]], pkg)

    return false
end

function try_install_aur_helper(retry_pkg)
    local install = utils.prompt("Do you want to install an AUR Helper? (y/n)", "y")
    if not install then return false end

    local default_helper = aur_helpers[1] -- yay
    if _try_info_pacman(default_helper)
    then ok = pacman.install(default_helper)
    else ok = install_via_makepkg(default_helper .. "-bin")
    end

    if not ok or not os.exists("/usr/bin/" .. default_helper) then
        cprint("Install %s failed", default_helper)
        return false
    end
    if retry_pkg then return install_via_helper(
        -- 为 yay 设置 builddir 以避免重复下载
        -- 此项在 paru 为 `--clonedir /path/to/dir`
        retry_pkg, {"--builddir", aur_pkgs_dir}
    ) else return true end
end

function _try_pacman_installed(name)
    return try {
        function() return pacman.installed(name) end,
        catch { function() return false end }
    }
end

function _try_info_pacman(name)
    return try {
        function() return pacman.info(name) ~= nil end,
        catch { function() return false end }
    }
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
    -- test
    install_via_makepkg("yay-bin")
end
