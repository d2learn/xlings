import("utils.archive")

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

    _set_install_file(xpkg)

    local ret = _try_execute_hook(xpkg.name, xpkg.hooks, "installed")
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
    local url = res.url
    local sha256 = res.sha256

    if not url then
        cprint("[xlings:xim]: ${dim}skip download (url is nil)${clear}")
        return true
    end

    -- TODO: impl independent download dir for env/vm
    local download_dir = runtime.get_xim_data_dir()

    -- 1. git clone
    if string.find(url, ".git", 1, true) then
        local pdir = path.join(download_dir, path.basename(url))
        os.exec("git clone --depth=1 " .. url .. " " .. pdir)
        runtime.set_pkginfo({ projectdir = pdir })
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
    return _try_execute_hook(xpkg.name, xpkg.hooks, "build")
end

function XPkgManager:install(xpkg)
    return _try_execute_hook(xpkg.name, xpkg.hooks, "install")
end

function XPkgManager:config(xpkg)
    return _try_execute_hook(xpkg.name, xpkg.hooks, "config")
end

function XPkgManager:uninstall(xpkg)
    _set_install_file(xpkg)
    return _try_execute_hook(xpkg.name, xpkg.hooks, "uninstall")
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
        { key = "license",      label = "license" },
        { key = "repo",         label = "repo" },
        { key = "docs",         label = "docs" },
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

function _set_install_file(xpkg)
    local url = xpkg:get_xpm_resources().url
    if url then
        local datadir = runtime.get_xim_data_dir()
        if string.find(url, ".git", 1, true) then
            local pdir = path.join(datadir, path.basename(url))
            runtime.set_pkginfo({ projectdir = pdir })
        else
            local filename = path.join(datadir, path.filename(url))
            runtime.set_pkginfo({ install_file = filename })
        end
    end
end

function _try_execute_hook(name, hooks, action)
    if hooks[action] then
        return hooks[action]()
    else
        cprint("[xlings:xim]: ${dim}package %s no implement${clear} %s", action, name)
    end
    return false
end
