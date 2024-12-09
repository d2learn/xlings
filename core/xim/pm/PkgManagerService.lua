import("xim.base.utils")
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

function PkgManagerService:create_pm_executor(pms)

    if pms.xpm then
        return PkgManagerExecutor.new(self._pmanagers.xpm)
    end

    local pm = nil
    for key, _ in pairs(t) do
        pm = self._pmanagers[key]
        if pm then
            break
        end
    end

    assert(pm, "package manager not found")

    return PkgManagerExecutor.new(pm)
end

function _dectect_and_load_pmanager()
    local pmanager = {
        xpm = XPkgManager.new(),
    }

    local os_type = utils.os_type()

    if os_type == "windows" then
        local winget = import("xim.pm.wrapper.winget")
        pmanager.winget = PkgManagerWrapper.new(winget)
    elseif os_type == "ubuntu" then
        local apt = import("xim.pm.wrapper.apt")
        pmanager.apt = PkgManagerWrapper.new(apt)
    elseif os_type == "arch linux" then
        -- TODO: add pacman
    else
        cprint("[xlings:xim]: local package manager not found on " .. os_type)
    end

    return pmanager
end

function main()
    local pms = _dectect_and_load_pmanager()
    print(pms)
end