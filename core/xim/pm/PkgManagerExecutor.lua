local PkgManagerExecutor = {}
PkgManagerExecutor.__index = PkgManagerExecutor

function new(pm)
    local instance = {}
    debug.setmetatable(instance, PkgManagerExecutor)
    instance.pm = pkg.pm
    return instance
end

function PkgManagerExecutor:support(xpkg)
    return _try_execute(self, "support", xpkg)
end

function PkgManagerExecutor:installed(xpkg)
    return _try_execute(self, "installed", xpkg)
end

function PkgManagerExecutor:deps(xpkg)
    return _try_execute(self, "deps", xpkg)
end

function PkgManagerExecutor:install(xpkg)
    return _try_execute(self, "install", xpkg)
end

function PkgManagerExecutor:config(xpkg)
    return _try_execute(self, "config", xpkg)
end

function PkgManagerExecutor:uninstall(xpkg)
    return _try_execute(self, "uninstall", xpkg)
end

function PkgManagerExecutor:info(xpkg)
    return _try_execute(self, "info", xpkg)
end

-- try to execute the action
function _try_execute(self, action, xpkg)
    return try {
        function()
            return self.pm[action](xpkg)
        end,
        catch {
            function(error)
                cprint("[xlings:xim]: %s", error)
                cprint(xpkg)
                cprint("${dim}---${clear}")
                cprint(self)
                cprint("[xlings:xim]: ${yellow}package manager runtime error - [action: %s] ${clear}", action)
                return false
            end
        }
    }
end