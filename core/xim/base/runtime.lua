-- runtime info
-- Note: import only init once times, inherit will init multiple times

-- TODO: optimize, use other method provide runtime info for package hooks
pkginfo = {
    version = "0.0.0",
    install_file = "xim-0.0.0.exe",
    projectdir = "repo",
}

xim_data_dir = {
    linux = tostring(os.getenv("HOME")) .. "/.xlings_data/xim",
    windows = "C:/Users/Public/.xlings_data/xim",
}

xim_data_dir = xim_data_dir[os.host()]

xim_debug_flag = false

if not os.isdir(xim_data_dir) then
    os.mkdir(xim_data_dir)
end

function get_pkginfo()
    return pkginfo
end

function set_pkginfo(info)
    if info.version then
        pkginfo.version = info.version
    end
    if info.install_file then
        pkginfo.install_file = info.install_file
    end
    if info.projectdir then
        pkginfo.projectdir = info.projectdir
    end
end

function get_xim_data_dir()
    return xim_data_dir
end

function xim_debug(value) -- setter/getter
    if value ~= nil then
        xim_debug_flag = value
    end
    return xim_debug_flag
end

function main()
    print(xim_data_dir)
end
