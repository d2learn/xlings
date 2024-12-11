-- runtime dir is cachedir

import("xim.base.runtime")
import("xim.pm.XPkgManager")

local PkgManagerExecutor = {}
PkgManagerExecutor.__index = PkgManagerExecutor

local gobal_xpm = XPkgManager.new()

function new(pm)
    local instance = {}
    debug.setmetatable(instance, PkgManagerExecutor)
    instance.type = pm.type
    instance._pm = pm
    return instance
end

function PkgManagerExecutor:installed(xpkg)
    os.cd(runtime.get_xim_data_dir())
    return _try_execute(self._pm, "installed", xpkg)
end

function PkgManagerExecutor:deps(xpkg)
    local deps = _try_execute(self._pm, "deps", xpkg)
    if self._pm.type == "wrapper" then
        print("\n---\n" .. deps .. "\n---\n")
        deps = gobal_xpm:deps(xpkg)
    end
    return deps
end

function PkgManagerExecutor:_download(xpkg)
    -- only for xpm, and called in install
    return _try_execute(self._pm, "download", xpkg)
end

function PkgManagerExecutor:_build(xpkg)
    -- only for xpm, and called in install
    if xpkg.hooks.build then
        cprint("[xlings:xim]: start build...")
        return _try_execute(self._pm, "build", xpkg)
    end
    return true
end

function PkgManagerExecutor:install(xpkg)
    -- reset to data dir

    os.cd(runtime.get_xim_data_dir())

    if self.type == "xpm" then
        if not self:_download(xpkg) or not self:_build(xpkg) then
            cprint("[xlings:xim]: hooks: ${red}download or build failed${clear}")
            return false
        end
    end

    cprint("[xlings:xim]: start install ${green}%s${clear}, it may take some minutes...", xpkg:name())
    if not _try_execute(self._pm, "install", xpkg) then
        cprint("[xlings:xim]: hooks.install: ${red}install failed${clear}")
        return false
    end

    if self.type == "xpm" then
        if not self:_config(xpkg) then
            cprint("[xlings:xim]: hooks.config: ${red}config failed${clear}")
            return false
        end
    end

    return true
end

function PkgManagerExecutor:_config(xpkg)
    -- only for xpm, and called in install
    if xpkg.hooks.config then
        cprint("[xlings:xim]: start config...", xpkg:name())
        return _try_execute(self._pm, "config", xpkg)
    end
    return true
end

function PkgManagerExecutor:uninstall(xpkg)
    return _try_execute(self._pm, "uninstall", xpkg)
end

function PkgManagerExecutor:info(xpkg)
    _try_execute(self._pm, "info", xpkg)
    if self._pm.type == "wrapper" then
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
                if action ~= "installed" then
                    cprint("[xlings:xim]: _try_execute: ${yellow}%s${clear}", error)
                    cprint("[xlings:xim]: ${yellow}package manager runtime error - [action: %s]${clear}", action)
                end
                return nil
            end
        }
    }
end