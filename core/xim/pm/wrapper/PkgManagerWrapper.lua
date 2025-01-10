local PkgManagerWrapper = {}
PkgManagerWrapper.__index = PkgManagerWrapper

function new(pm)
    local instance = {}
    debug.setmetatable(instance, PkgManagerWrapper)
    instance.type = "wrapper"
    instance.pm = pm
    return instance
end

function PkgManagerWrapper:installed(pkg)
    return self.pm.installed(pkg.name)
end

function PkgManagerWrapper:deps(pkg)
    self.pm.deps(pkg.name)
    return {}
end

function PkgManagerWrapper:install(pkg)
    return self.pm.install(pkg.name)
end

function PkgManagerWrapper:uninstall(pkg)
    return self.pm.uninstall(pkg.name)
end

function PkgManagerWrapper:info(pkg)
    local info = self.pm.info(pkg.name)
    cprint(info)
    cprint([[

--- ${cyan}info${clear}

${bright}name:${clear} ${dim}%s${clear}
${bright}pmwrapper:${clear} ${dim}%s${clear}
    ]], pkg.name, pkg.pmwrapper)
end