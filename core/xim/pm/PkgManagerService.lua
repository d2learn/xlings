import("base.utils")
import("pm.XPackage")
import("pm.XPkgManager")
import("pm.wrapper.PkgManagerWrapper")
import("pm.PkgManagerExecutor")

local PkgManagerService = {}
PkgManagerService.__index = PkgManagerService

function new()
    if not PkgManagerService._pmanagers then
        PkgManagerService._pmanagers = _dectect_and_load_pmanager()
    end
    return PkgManagerService
end

function PkgManagerService:create_pm_executor(pkg)

    if not pkg.pmwrapper then
        return PkgManagerExecutor.new(
            self._pmanagers.xpm,
            XPackage.new(pkg)
        )
    end

    if not self._pmanagers[pkg.pmwrapper] then
        cprint("[xlings:xim]: local package manager not found")
    end

    return PkgManagerExecutor.new(self._pmanagers[pkg.pmwrapper], pkg)
end

function _dectect_and_load_pmanager()
    local pmanager = {
        xpm = XPkgManager.new(),
    }

    local pm = utils.local_package_manager()
    if pm then
        local pm_impl = import("pm.wrapper." .. pm)
        pmanager[pm] = PkgManagerWrapper.new(pm_impl)

        if pm == "pacman" then
            local pm_aur_impl = import("pm.wrapper.aur")
            pmanager["aur"] = PkgManagerWrapper.new(pm_aur_impl)
        end

    end

    return pmanager
end

function main()
    local pms = _dectect_and_load_pmanager()
    print(pms)
end