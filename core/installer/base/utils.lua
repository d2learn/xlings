local installer_base_dir = path.directory(os.scriptdir())

function load_installers(dir)
    local installers = {}

    local installers_dir = path.join(installer_base_dir, dir)

    for _, file in ipairs(os.files(path.join(installers_dir, "*.lua"))) do
        local name = path.basename(file)
        local installer = import("installer." .. dir .. "." .. name)
        installers[name] = installer
    end

    return installers
end

function main()
    --local i = get_linux_distribution()
    --print(i)
end