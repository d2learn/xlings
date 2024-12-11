local installer_base_dir = path.directory(os.scriptdir())

function load_installers(dir)
    local installers = {}

    local installers_dir = path.join(installer_base_dir, dir)

    for _, file in ipairs(os.files(path.join(installers_dir, "*.lua"))) do
        local name = path.basename(file)
        local installer = import("xim." .. dir .. "." .. name)
        installers[name] = installer
    end

    return installers
end

function deep_copy(orig)
    local copies = {}

    local function _copy(obj)
        if type(obj) ~= "table" then
            return obj
        end

        if copies[obj] then
            return copies[obj]
        end

        local new_table = {}
        copies[obj] = new_table

        for key, value in pairs(obj) do
            new_table[_copy(key)] = _copy(value)
        end

        return debug.setmetatable(new_table, debug.getmetatable(obj))
    end

    return _copy(orig)
end

function os_type()
    local os_type = os.host()
    
    if os_type == "linux" then
        os_type = linuxos.name()
        -- os_version = linuxos.version() -- TODO: get linux version
    end

    return os_type
end

function os_info()
    local os_type = os.host()
    local name, version = "", ""

    if os_type == "linux" then
        name = linuxos.name()
        version = linuxos.version() -- TODO: get linux version
    elseif os_type == "windows" then
        name = winos.name()
        version = winos.version()
    elseif os_type == "macosx" then
        name = macosxos.name()
        version = macos.version()
    end

    return {
        name = name,
        version = version
    }
end

function main()
    print("\n\t test deep_copy \n")
    local mytabel = {
        ["a"] = 1,
        ["b"] = 2,
        ["c"] = {
            ["d"] = 3,
            ["e"] = 4
        }
    }
    local mytabel2 = deep_copy(mytabel)
    mytabel2["c"]["d"] = 5
    print(mytabel["c"]["d"])

    print(os_type())
    print(os_info())
end