-- runtime info
-- Note: import only init once times, inherit will init multiple times

-- TODO: optimize, use other method provide runtime info for package hooks
pkginfo = {
    version = "0.0.0",
    install_file = "xim-0.0.0.exe",
}

xim_data_dir = {
    linux = os.getenv("HOME") .. "/.xlings_data/xim",
    windows = "C:/Users/Public/.xlings_data/xim",
}

xim_data_dir = xim_data_dir[os.host()]

if not os.isdir(xim_data_dir) then
    os.mkdir(xim_data_dir)
end

function get_pkginfo()
    return pkginfo
end

function set_pkginfo(info)
    pkginfo = info
end

function get_xim_data_dir()
    return xim_data_dir
end

function main()
    print(xim_data_dir)
end