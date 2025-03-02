-- runtime info
-- Note: import only init once times, inherit will init multiple times

-- TODO: optimize, use other method provide runtime info for package hooks
import("platform")

pkginfo = {
    name = "xpkg-name",
    version = "0.0.0",
    install_file = "xim-0.0.0.exe",
    install_dir = "name/version",
}

xim_data_dir = {
    linux = "/home/xlings/.xlings_data/xim",
    windows = "C:/Users/Public/.xlings_data/xim",
}

xim_data_dir = xim_data_dir[os.host()]
xim_install_basedir = path.join(xim_data_dir, "xpkgs")

if not os.isdir(xim_data_dir) then
    os.mkdir(xim_data_dir)
end

function get_pkginfo()
    return pkginfo
end

function set_pkginfo(info)
    if info.name then
        pkginfo.name = info.name
    end
    if info.version then
        pkginfo.version = info.version
    end
    if info.install_file then
        pkginfo.install_file = info.install_file
    end
    if info.install_dir then
        pkginfo.install_dir = info.install_dir
    end
end

function get_rundir()
    local rundir = platform.get_config_info().rundir
    if rundir == nil or not os.isdir(rundir) then
        rundir = path.absolute(".")
    end
    return rundir
end

function get_xim_data_dir()
    return xim_data_dir
end

function get_xim_install_basedir()
    return xim_install_basedir
end

function main()
    print(xim_data_dir)
end
