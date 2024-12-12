local PkgManagerWrapper = {}
PkgManagerWrapper.__index = PkgManagerWrapper

function new(pm)
    local instance = {}
    debug.setmetatable(instance, PkgManagerWrapper)
    instance.type = "wrapper"
    instance.pm = pm
    return instance
end

function PkgManagerWrapper:installed(xpkg)
    return self.pm.installed(xpkg:get_map_pkgname())
end

function PkgManagerWrapper:deps(xpkg)
    return self.pm.deps(xpkg:get_map_pkgname())
end

function PkgManagerWrapper:install(xpkg)
    return self.pm.install(xpkg:get_map_pkgname())
end

function PkgManagerWrapper:uninstall(xpkg)
    return self.pm.uninstall(xpkg:get_map_pkgname())
end

function PkgManagerWrapper:info(xpkg)
    local info = self.pm.info(xpkg:get_map_pkgname())
    print(info)
end