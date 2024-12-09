local XPkgManager = {}
XPkgManager.__index = XPkgManager

-- package: xim package spec: todo
function new()
    local instance = {}
    debug.setmetatable(instance, XPkgManager)
    instance.type = "xpm"
    return instance
end

function XPkgManager:support(xpkg)
    return xpkgs.support()
end

function XPkgManager:installed(xpkg)
    return _try_execute_hook(xpkg:name(), xpkg.hooks, "installed")
end

function XPkgManager:download(xpkg)
    local source = xpkg:source()
    local url = source.url
    local sha256 = source.sha256

    -- TODO

    return true
end

function XPkgManager:deps(xpkg)
    local deps = xpkg:deps()

    -- TODO: analyze deps by semver

    return deps
end

function XPkgManager:build(xpkg)
    return _try_execute_hook(xpkg:name(), xpkg.hooks, "build")
end

function XPkgManager:install(xpkg)
    return _try_execute_hook(xpkg:name(), xpkg.hooks, "install")
end

function XPkgManager:config(xpkg)
    return _try_execute_hook(xpkg:name(), xpkg.hooks, "config")
end

function XPkgManager:uninstall(xpkg)
    return _try_execute_hook(xpkg:name(), xpkg.hooks, "uninstall")
end

function XPkgManager:info(xpkg)
    local info = xpkg:info()

    cprint("\n--- ${cyan}info${clear}")

    local fields = {
        {key = "name", label = "name"},
        {key = "homepage", label = "homepage"},
        {key = "version", label = "version"},
        {key = "author", label = "author"},
        {key = "maintainer", label = "maintainer"},
        {key = "contributor", label = "contributor"},
        {key = "license", label = "license"},
        {key = "repo", label = "repo"},
        {key = "docs", label = "docs"},
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
        cprint( "\t${green bright}" .. info["description"] .. "${clear}")
    end

    cprint("")
end

function _try_execute_hook(name, hooks, action)
    if hooks[action] then
        return hooks[action]()
    else
        cprint("[xlings:xim]: ${yellow}package %s no implement${clear} %s", action, name)
    end
    return false
end