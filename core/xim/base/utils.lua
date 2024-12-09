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
end