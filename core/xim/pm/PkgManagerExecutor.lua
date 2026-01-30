-- runtime dir is cachedir

import("xim.base.runtime")
import("xim.base.xvm")
import("xim.pm.XPkgManager")

local PkgManagerExecutor = {}
PkgManagerExecutor.__index = PkgManagerExecutor

function new(pm, pkg)
    local instance = {}
    debug.setmetatable(instance, PkgManagerExecutor)
    instance.type = pm.type
    instance._pm = pm
    instance._pkg = pkg -- XPackage or Local Package
    return instance
end

function PkgManagerExecutor:support()
    return self._pm ~= nil
end

function PkgManagerExecutor:installed()
    os.cd(runtime.get_runtime_dir())
    return _try_execute(self, "installed")
end

function PkgManagerExecutor:deps()
    local deps = _try_execute(self, "deps")
    return deps
end

function PkgManagerExecutor:_download()
    -- only for xpm, and called in install
    return _try_execute(self, "download")
end

function PkgManagerExecutor:_build()
    -- only for xpm, and called in install
    if self._pkg.hooks.build then
        cprint("[xlings:xim]: start build...")
        return _try_execute(self, "build")
    end
    return true
end

function PkgManagerExecutor:install()
    -- reset to data dir

    os.cd(runtime.get_runtime_dir())

    if self.type == "xpm" then
        if not self:_download() or not self:_build() then
            cprint("[xlings:xim]: hooks: ${red}download or build failed${clear}")
            return false
        end
    end

    cprint("[xlings:xim]: start install ${green}%s${clear}, it may take some minutes...", self._pkg.name)
    if not _try_execute(self, "install") then
        cprint("[xlings:xim]: hooks.install: ${red}install failed${clear}")
        return false
    end

    if self.type == "xpm" then
        if not self:_config() then
            cprint("[xlings:xim]: hooks.config: ${red}config failed${clear}")
            return false
        end
    end

    return true
end

function PkgManagerExecutor:use()
    xvm.use(self._pkg.name, self._pkg.version)
end

function PkgManagerExecutor:_config()
    -- only for xpm, and called in install
    if self._pkg.hooks.config then
        cprint("[xlings:xim]: start config...", self._pkg.name)
        return _try_execute(self, "config")
    end
    return true
end

function PkgManagerExecutor:uninstall()
    os.cd(runtime.get_runtime_dir())
    return _try_execute(self, "uninstall")
end

function PkgManagerExecutor:info()
    _try_execute(self, "info")
end

-- try to execute the action
function _try_execute(pme, action)
    local pm = pme._pm
    local pkg = pme._pkg
    runtime.set_pkginfo({version = pme._pkg.version})
    return try {
        function()
            --cprint("[xlings:xim]: execute - [action: %s]", action)
            return pm[action](pm, pkg)
        end,
        catch {
            function(error)
                if action ~= "installed" then
                    cprint("[xlings:xim]: _try_execute:${yellow}%s${clear}", error)
                    cprint("[xlings:xim]: ${yellow}package manager runtime error - [action: %s]${clear}", action)
                end
                return nil
            end
        }
    }
end