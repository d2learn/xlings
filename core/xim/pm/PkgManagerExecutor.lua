-- runtime dir is cachedir

import("xim.pm.XPkgManager")

local PkgManagerExecutor = {}
PkgManagerExecutor.__index = PkgManagerExecutor

local gobal_xpm = XPkgManager.new()

function new(pm)
    local instance = {}
    debug.setmetatable(instance, PkgManagerExecutor)
    instance.pm = pm
    return instance
end

function PkgManagerExecutor:support(xpkg)
    return _try_execute(self.pm, "support", xpkg)
end

function PkgManagerExecutor:installed(xpkg)
    return _try_execute(self.pm, "installed", xpkg)
end

function PkgManagerExecutor:deps(xpkg)
    local deps = _try_execute(self.pm, "deps", xpkg)
    if self.pm.type == "wrapper" then
        print("\n---\n" .. deps .. "\n---\n")
        deps = gobal_xpm:deps(xpkg)
    end
    return deps
end

function PkgManagerExecutor:download(xpkg)
    if self.pm.type == "xpm" then
        return _try_execute(self.pm, "download", xpkg)
    else
        return true
    end
end

function PkgManagerExecutor:build(xpkg)
    if self.pm.type == "xpm" then
        return _try_execute(self.pm, "build", xpkg)
    else
        return true
    end
end

function PkgManagerExecutor:install(xpkg)
    return _try_execute(self.pm, "install", xpkg)
end

function PkgManagerExecutor:config(xpkg)
    if self.pm.type == "xpm" then
        return _try_execute(self.pm, "config", xpkg)
    else
        return true
    end
end

function PkgManagerExecutor:uninstall(xpkg)
    return _try_execute(self.pm, "uninstall", xpkg)
end

function PkgManagerExecutor:info(xpkg)
    _try_execute(self.pm, "info", xpkg)
    if self.pm.type == "wrapper" then
        gobal_xpm:info(xpkg)
    end
end

-- try to execute the action
function _try_execute(pm, action, xpkg)
    return try {
        function()
            --cprint("[xlings:xim]: execute - [action: %s]", action)
            return pm[action](pm, xpkg)
        end,
        catch {
            function(error)
                cprint("[xlings:xim]: _try_execute: %s", error)
                cprint(xpkg)
                cprint("${dim}---${clear}")
                cprint(pm)
                cprint("[xlings:xim]: ${yellow}package manager runtime error - [action: %s] ${clear}", action)
                return nil
            end
        }
    }
end