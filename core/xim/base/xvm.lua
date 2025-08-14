import("privilege.sudo")

import("xim.base.runtime")

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

    local pkginfo = runtime.get_pkginfo()

    local version = config.version or pkginfo.version
    local bindir = config.bindir or pkginfo.install_dir
    local envs = config.envs or {}

    name = name or pkginfo.name

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
        local program = path.join(runtime.get_bindir(), name)
        if not os.isfile(symbol) then
            sudo.exec("ln -s %s %s", program, symbol)
            cprint("[xlings:xim]: symbol [${green}%s${clear}] created", symbol)
        else
            cprint("[xlings:xim]: ${yellow dim}symbol [%s] already exists, skip...", symbol)
        end
    end

end

function use(name, version)
    local pkginfo = runtime.get_pkginfo()
    name = name or pkginfo.name
    version = version or pkginfo.version
    __xvm_run("use " .. name .. " " .. version)
end

function remove(name, version)

    local pkginfo = runtime.get_pkginfo()

    name = name or pkginfo.name
    version = version or pkginfo.version
    __xvm_run("remove " .. name .. " " .. version)

    -- TODO: remove symbol ...

end

function info(name, version)

    local pkginfo = runtime.get_pkginfo()

    name = name or pkginfo.name
    version = version or pkginfo.version or ""
    local output = __xvm_run(
        "info " .. name .. " " .. version,
        { return_value = true }
    )

    local info_table = {
        keys = { "Name", "Version", "Type", "Program", "SPath", "TPath", "Alias" },
        ["Program"] = nil,
        ["Version"] = nil,
        ["Type"] = nil,
        ["SPath"] = nil,
        ["TPath"] = nil,
        ["Alias"] = nil,
        --["Envs"] = { },
    }

    local envs_flag = false

    for line in output:gmatch("[^\r\n]+") do
        if line:trim() == "Envs:" then
            envs_flag = true
            info_table["Envs"] = { }
        elseif envs_flag then
            local str_arr = line:trim():split("=")
            if #str_arr == 2 then
                local key = str_arr[1]:trim()
                local value = str_arr[2]:trim()
                info_table["Envs"][key] = value
            end
        else
            for _, k in ipairs(info_table.keys) do
                if line:find(k) then
                    local value = line:split(":")[2]:trim()
                    info_table[k] = value
                end
            end
        end
    end

    return info_table
end

function __xvm_run(cmd, opt)

    opt = opt or { }

    local xvm_path = path.join(runtime.get_bindir(), "xvm")
    if is_host("windows") then
        xvm_path = path.join(runtime.get_bindir(), "xvm.exe")
    end
    if not os.isfile(xvm_path) then
        os.exec("xlings install xvm -y")
    end
    xvm_cmd = string.format([[%s %s]], xvm_path, cmd)
    cprint("[xlings:xim]: xvm run - ${dim}%s", xvm_cmd)
    return try {
        function ()
            if opt.return_value then
                return os.iorun(xvm_cmd)
            else
                os.exec(xvm_cmd)
            end
        end,
        catch {
            function (e)
                cprint("[xlings:xim]: xvm run failed - ${red}%s", e)
            end
        }
    }
end

function test_main()
    local pinfo = info("tttt", "0.0.2");
    print(pinfo.Program)
    print(pinfo.SPath)
end