import("devel.git")
import("utils.archive")
import("lib.detect.find_tool")
import("privilege.sudo")

import("base.github")
import("base.utils")
import("base.runtime")
import("base.xvm")

import("pm.types")

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
    elseif xpkg.type == "template" then
        return types.template.installed(xpkg)
    else
        local old_value = xvm.log_tag(false)
        ret = xvm.has(xpkg.name, xpkg.version)
        xvm.log_tag(old_value)
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

    local url = utils.try_mirror_match_for_url(res.url)
    local sha256 = res.sha256

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
    local download_dir = runtime.get_runtime_dir()

    -- 1. git clone
    if string.find(url, "%.git$") then
        local pdir = path.join(download_dir, path.basename(url))
        cprint("[xlings:xim]: clone %s...", url)
        runtime.set_pkginfo({ install_file = pdir })
        git.clone(url, {verbose = true, depth = 1, recursive = true, outputdir = pdir})
    else
        local ok, filename = utils.try_download_and_check(url, download_dir, sha256)

        -- pre-set pkginfo for install_file, avoid clear failed when file currupted
        runtime.set_pkginfo({ install_file = filename })

        if not ok then -- retry download
            cprint("[xlings:xim]: ${yellow}download failed, start to detect all mirror and retry...${clear}")
            url = utils.try_mirror_match_for_url(res.url, { detect = true })
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
    if not xpkg.hooks["install"] then
        if xpkg.type == "script" then
            xpkg.hooks["install"] = function()
                return types.script.install(xpkg)
            end
        elseif xpkg.type == "template" then
            xpkg.hooks["install"] = function()
                return types.template.install(xpkg)
            end
        end
    end
    return _try_execute_hook(xpkg.name, xpkg, "install")
end

function XPkgManager:config(xpkg)
    return _try_execute_hook(xpkg.name, xpkg, "config")
end

function XPkgManager:uninstall(xpkg)

    if not xpkg.hooks["uninstall"] then
        if xpkg.type == "script" then
            xpkg.hooks["uninstall"] = function()
                return types.script.uninstall(xpkg)
            end
        elseif xpkg.type == "template" then
            xpkg.hooks["uninstall"] = function()
                return types.template.uninstall(xpkg)
            end
        end
    end

    local ret = _try_execute_hook(xpkg.name, xpkg, "uninstall")

    local installdir = runtime.get_pkginfo().install_dir
    cprint("[xlings:xim]: try remove - ${dim}%s", installdir)
    if not os.tryrm(installdir) and os.isdir(installdir) then
        cprint("[xlings:xim]: ${yellow}warning: remove install dir failed - %s${clear}", installdir)
        cprint("[xlings:xim]: ${yellow}try again by sudo...${clear}")
        sudo.exec("rm -rf " .. installdir)
    end
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
        { key = "namespace",    label = "namespace" },
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
        if type(value) == "table" then
            value = table.concat(value, ", ")
        end
        if value then
            cprint(string.format("${bright}%s:${clear} ${dim}%s${clear}", field.label, value))
        end
    end

    if info["programs"] then
        local programs = table.concat(info["programs"], ", ")
        cprint(string.format("${bright}programs:${clear} ${dim cyan}%s${clear}", programs))
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
        if action == "install" then
            local install_dir = runtime.get_pkginfo().install_dir
            if not os.isdir(install_dir) then
                cprint("[xlings:xim]: ${dim}create install dir %s${clear}", install_dir)
                os.mkdir(install_dir)
            end
        end
        return xpkg.hooks[action]()
    else
        cprint("[xlings:xim]: ${dim}package %s no implement${clear} %s", action, name)
    end
    return false
end

function _set_runtime_info(xpkg)

    local pkginfo = runtime.get_pkginfo()
    local namespace_equal = (xpkg.namespace == pkginfo.namespace)
    local name_equal = (xpkg.name == pkginfo.name)
    local version_equal = (xpkg.version == pkginfo.version)

    if namespace_equal and name_equal and version_equal then return end

    local url = xpkg:get_xpm_resources().url
    url = utils.try_mirror_match_for_url(url)

    local runtimedir = runtime.get_runtime_dir()
    local pkgname = xpkg.name
    if xpkg.namespace then
        pkgname = xpkg.namespace .. "-x-" .. pkgname
    end
    local install_dir = path.join(runtime.get_xim_install_basedir(), pkgname, xpkg.version)

    if url then
        local filename = path.join(runtimedir, path.filename(url))
        -- TODO: filename:replace("git", ""):replace(".tar.gz", "") ...?
        --      or add new field to xpkg? project_dir?
        runtime.set_pkginfo({ install_file = filename })
    end

    runtime.set_pkginfo({
        name = xpkg.name,
        namespace = xpkg.namespace,
        install_dir = install_dir
    })

end
