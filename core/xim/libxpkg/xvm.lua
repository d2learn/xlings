import("privilege.sudo")

import("xim.base.runtime")

import("xim.libxpkg.system")
import("xim.libxpkg.pkginfo")

--[[
config = {
    version = "1.0.0",
    bindir = "/path/to/package",
    alias = "cmd",
    envs = {
        varname = "value",
        ...
    },
}
]]
function add(name, config)
    config = config or { }

    local version = config.version or pkginfo.version()
    local bindir = config.bindir or pkginfo.install_dir()
    local envs = config.envs or {}

    name = name or pkginfo.name()

    local alias_arg = ""
    if config.alias then
        alias_arg = string.format([[ --alias "%s"]], config.alias)
    end

    local envs_args = ""
    for k, v in pairs(envs) do
        envs_args = envs_args .. string.format([[ --env "%s=%s" ]], k, v)
    end

    __xvm_run(string.format(
        [[add %s %s --path %s %s %s]],
        name, version, bindir,
        alias_arg, envs_args
    ))

    input_args = runtime.get_runtime_data().input_args
    if input_args.enable_global and is_host("linux") then
        cprint("[xlings:xim]: enable [${green}%s${clear}] for global...", name)
        io.stdout:flush()

        local symbol = path.join("/usr/bin", name)
        local program = path.join(system.bindir(), name)
        if not os.isfile(symbol) then
            sudo.exec("ln -s %s %s", program, symbol)
            cprint("[xlings:xim]: symbol [${green}%s${clear}] created", symbol)
        else
            cprint("[xlings:xim]: ${yellow dim}symbol [%s] already exists, skip...", symbol)
        end
    end

end

function use(name, version)
    name = name or pkginfo.name()
    version = version or pkginfo.version()
    __xvm_run("use " .. name .. " " .. version)
end

function remove(name, version)
    name = name or pkginfo.name()
    version = version or pkginfo.version()
    __xvm_run("remove " .. name .. " " .. version)

    -- TODO: remove symbol ...

end

function __xvm_run(cmd)
    local xvm_path = path.join(system.bindir(), "xvm")
    if is_host("windows") then
        xvm_path = path.join(system.bindir(), "xvm.exe")
    end
    if not os.isfile(xvm_path) then
        os.exec("xlings install xvm -y")
    end
    xvm_cmd = string.format([[%s %s]], xvm_path, cmd)
    cprint("[xlings:xim]: xvm run - ${dim}%s", xvm_cmd)
    os.exec(xvm_cmd)
end