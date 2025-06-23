import("devel.git")
import("utils.archive")
import("lib.detect.find_tool")

import("config.xconfig")

import("xim.base.github")
import("xim.base.utils")
import("xim.base.runtime")

local XPkgManager = {}
XPkgManager.__index = XPkgManager

-- package: xim package spec: todo
function new()
    local instance = {}
    debug.setmetatable(instance, XPkgManager)
    instance.type = "xpm"
    return instance
end

function XPkgManager:installed(xpkg)
    local ret = nil

    if xpkg.hooks["installed"] then
        ret = _try_execute_hook(xpkg.name, xpkg, "installed")
    else
        local xvm_bin = path.join(runtime.get_bindir(), "xvm")
        if is_host("windows") then
            xvm_bin = path.join(runtime.get_bindir(), "xvm.exe")
        end
        if os.isfile(xvm_bin) then
            ret = os.iorun([[%s list %s]], xvm_bin, xpkg.name):trim()
        end
    end

    if type(ret) == "boolean" then
        return ret
    elseif type(ret) == "string" then
        return string.find(ret, xpkg.version, 1, true) ~= nil
    else
        return false
    end
end

function XPkgManager:download(xpkg)
    local res = xpkg:get_xpm_resources()
    local url
    local sha256

    if res == "XLINGS_RES" then
        --"https://github.com/xlings-res/xvm/releases/download/0.0.4/xvm-0.0.4-linux.tar.gz"
        --"https://gitcode.com/xlings-res/xvm/releases/download/0.0.4/xvm-0.0.4-linux.tar.gz"
        local res_url_template = [[%s/%s/releases/download/%s/%s-%s-%s-%s.%s]]
        local res_server = xconfig.load().xim["res-server"]
        local pkgname = xpkg.name
        local pkgver = xpkg.version
        local osname = xpkg._real_os_key -- TODO: optimize this
        local file_ext = (osname == "windows") and "zip" or "tar.gz"
        local os_arch = os.arch()
        if os_arch == "x64" then os_arch = "x86_64" end
        url = string.format(
            res_url_template,
            res_server,
            pkgname,
            pkgver,
            pkgname, pkgver, osname, os_arch, file_ext
        )
    else
        url = utils.try_mirror_match_for_url(res.url)
        sha256 = res.sha256
    end

    if not url then
        cprint("[xlings:xim]: ${dim}skip download (url is nil)${clear}")
        return true
    end

    if res.github_release_tag then
        local user, project = xpkg:info().repo:match("github%.com/([%w-_]+)/([%w-_]+)")
        -- url is a pattern, when github_release_tag is not nil
        local info = github.get_release_tag_info(user, project, res.github_release_tag, url)
        url = info.url
    end

    -- TODO: impl independent download dir for env/vm
    local download_dir = runtime.get_xim_data_dir()

    -- 1. git clone
    if string.find(url, "%.git$") then
        local pdir = path.join(download_dir, path.basename(url))
        cprint("[xlings:xim]: clone %s...", url)
        runtime.set_pkginfo({ install_file = pdir })
        git.clone(url, {verbose = true, depth = 1, recursive = true, outputdir = pdir})
    else
        local ok, filename = utils.try_download_and_check(url, download_dir, sha256)
        if not ok then -- retry download
            ok, filename = utils.try_download_and_check(url, download_dir, sha256)
        end

        if ok then
            if utils.is_compressed(filename) then
                cprint("[xlings:xim]: start extract %s${clear}", path.filename(filename))
                archive.extract(filename, download_dir)
            end
        else
            -- download failed or sha256 check failed
            cprint("[xlings:xim]: ${red}download or sha256-check failed${clear}")
            return false
        end

        runtime.set_pkginfo({ install_file = filename })
    end

    return true
end

function XPkgManager:deps(xpkg)
    local deps = xpkg:get_deps()

    -- TODO: analyze deps by semver

    return deps or {}
end

function XPkgManager:build(xpkg)
    return _try_execute_hook(xpkg.name, xpkg, "build")
end

function XPkgManager:install(xpkg)
    return _try_execute_hook(xpkg.name, xpkg, "install")
end

function XPkgManager:config(xpkg)
    return _try_execute_hook(xpkg.name, xpkg, "config")
end

function XPkgManager:uninstall(xpkg)
    local ret = _try_execute_hook(xpkg.name, xpkg, "uninstall")
    os.tryrm(runtime.get_pkginfo().install_dir)
    return ret
end

function XPkgManager:info(xpkg)
    local info = xpkg:info()

    info.type = info.type or "package"

    cprint("\n--- [${magenta bright}%s${clear}] ${cyan}info${clear}", info.type)

    local fields = {
        { key = "name",         label = "name" },
        { key = "homepage",     label = "homepage" },
        { key = "version",      label = "version" },
        { key = "authors",      label = "authors" },
        { key = "maintainers",  label = "maintainers" },
        { key = "contributors", label = "contributors" },
        { key = "licenses",     label = "licenses" },
        { key = "repo",         label = "repo" },
        { key = "docs",         label = "docs" },
        { key = "forum",        label = "forum" },
    }

    cprint("")
    for _, field in ipairs(fields) do
        local value = info[field.key]
        if value then
            cprint(string.format("${bright}%s:${clear} ${dim}%s${clear}", field.label, value))
        end
    end

    cprint("")

    if info["description"] then
        cprint("\t${green bright}" .. info["description"] .. "${clear}")
    end

    cprint("")
end

function _try_execute_hook(name, xpkg, action)
    if xpkg.hooks[action] then
        _set_runtime_info(xpkg)
        return xpkg.hooks[action]()
    else
        cprint("[xlings:xim]: ${dim}package %s no implement${clear} %s", action, name)
    end
    return false
end

function _set_runtime_info(xpkg)
    if runtime.get_pkginfo().name == xpkg.name then return end

    local url = xpkg:get_xpm_resources().url
    url = utils.try_mirror_match_for_url(url)

    local datadir = runtime.get_xim_data_dir()
    local install_dir = path.join(runtime.get_xim_install_basedir(), xpkg.name, xpkg.version)

    if url then
        local filename = path.join(datadir, path.filename(url))
        runtime.set_pkginfo({ install_file = filename })
    end

    if not os.isdir(install_dir) then
        os.mkdir(install_dir)
    end

    runtime.set_pkginfo({
        name = xpkg.name,
        install_dir = install_dir
    })

end
