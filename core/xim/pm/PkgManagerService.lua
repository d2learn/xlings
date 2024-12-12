import("xim.base.utils")
import("xim.pm.XPackage")
import("xim.pm.XPkgManager")
import("xim.pm.wrapper.PkgManagerWrapper")
import("xim.pm.PkgManagerExecutor")

local PkgManagerService = {}
PkgManagerService.__index = PkgManagerService

function new()
    if not PkgManagerService._pmanagers then
        PkgManagerService._pmanagers = _dectect_and_load_pmanager()
    end
    return PkgManagerService
end

function PkgManagerService:create_pm_executor(pkg)

    xpkg = XPackage.new(pkg)

    if xpkg:xpm_enable() then
        return PkgManagerExecutor.new(self._pmanagers.xpm, xpkg)
    end

    local pm = xpkg:get_pm_wrapper()
    if not self._pmanagers[pm] then
        cprint("[xlings:xim]: local package manager not found")
    end

    return PkgManagerExecutor.new(self._pmanagers[pm], xpkg)
end

function _dectect_and_load_pmanager()
    local pmanager = {
        xpm = XPkgManager.new(),
    }

    local pm = utils.local_package_manager()
    if pm then
        local pm_impl = import("xim.pm.wrapper." .. pm)
        pmanager[pm] = PkgManagerWrapper.new(pm_impl)
    end

    return pmanager
end

function main()
    local pms = _dectect_and_load_pmanager()
    print(pms)
end