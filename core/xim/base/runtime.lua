-- runtime info
-- Note: import only init once times, inherit will init multiple times

-- TODO: optimize, use other method provide runtime info for package hooks
import("platform")

xim_data_dir = path.join(platform.get_config_info().rcachedir, "xim")

xim_runtime_data = {
    pkginfo = {
        name = "xpkg-name",
        version = "0.0.0",
        install_file = "xim-0.0.0.exe",
        install_dir = "name/version",
    },
    input_args = {},
    --rundir = "",
}

xim_install_basedir = path.join(xim_data_dir, "xpkgs")
xim_index_reposdir = path.join(xim_data_dir, "xim-index-repos")
xim_local_index_repodir = path.join(xim_data_dir, "local-indexrepo")

function init()
    -- create xim data dir
    if not os.isdir(xim_data_dir) then
        os.mkdir(xim_data_dir)
    end
    -- create xim install basedir
    if not os.isdir(xim_install_basedir) then
        os.mkdir(xim_install_basedir)
    end
    -- create xim index repos dir
    if not os.isdir(xim_index_reposdir) then
        os.mkdir(xim_index_reposdir)
    end
    -- create xim local index repo dir
    if not os.isdir(xim_local_index_repodir) then
        os.mkdir(xim_local_index_repodir)
    end
end

function get_runtime_data()
    return xim_runtime_data
end

function set_runtime_data(data)
    if data.pkginfo then
        set_pkginfo(data.pkginfo)
    end
    if data.input_args then
        xim_runtime_data.input_args = data.input_args
    end
end

function get_pkginfo()
    return xim_runtime_data.pkginfo
end

function set_pkginfo(info)
    if info.name then
        xim_runtime_data.pkginfo.name = info.name
    end
    if info.version then
        xim_runtime_data.pkginfo.version = info.version
    end
    if info.install_file then
        xim_runtime_data.pkginfo.install_file = info.install_file
    end
    if info.install_dir then
        xim_runtime_data.pkginfo.install_dir = info.install_dir
    end
end

function get_rundir()
    local rundir = platform.get_config_info().rundir
    if rundir == nil or not os.isdir(rundir) then
        rundir = path.absolute(".")
    end
    return rundir
end

function get_bindir()
    return platform.get_config_info().bindir
end

function get_xim_data_dir()
    return xim_data_dir
end

function get_xim_install_basedir()
    return xim_install_basedir
end

function get_xim_index_reposdir()
    return xim_index_reposdir
end

function get_xim_local_index_repodir()
    return xim_local_index_repodir
end

function main()
    print(xim_data_dir)
end
