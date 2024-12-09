local PkgManagerWrapper = {}
PkgManagerWrapper.__index = PkgManagerWrapper

function new(pm)
    local instance = {}
    debug.setmetatable(instance, PkgManagerWrapper)
    instance.type = "wrapper"
    instance.pm = pm
    return instance
end

function PkgManagerWrapper:support(xpkg)
end

function PkgManagerWrapper:installed(xpkg)
end

function PkgManagerWrapper:deps(xpkg)
end

function PkgManagerWrapper:install(xpkg)
end

function PkgManagerWrapper:config(xpkg)
end

function PkgManagerWrapper:uninstall(xpkg)
end

function PkgManagerWrapper:info(xpkg)
end