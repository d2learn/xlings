-- runtime info
-- Note: import only init once times, inherit will init multiple times

-- TODO: optimize, use other method provide runtime info for package hooks
import("platform")

__xim_data_dir = nil
__xim_runtime_dir = nil

xim_runtime_data = {
    pkginfo = {
        name = "xpkg-name",
        version = "0.0.0",
        namespace = "xim",
        install_file = "xim-0.0.0.exe",
        install_dir = "namespace-x-name/version",
        deps_list = {},
        elfpatch_auto = false,
        elfpatch_shrink = false,
    },
    input_args = {},
    rundir = "",
}

__xim_install_basedir = nil
__xim_index_reposdir = nil
__xim_local_index_repodir = nil

function init()
    -- create xim data dir
    if not os.isdir(get_xim_data_dir()) then
        os.mkdir(get_xim_data_dir())
    end

    if not os.isdir(get_runtime_dir()) then
        os.mkdir(get_runtime_dir())
    end

    -- create xim install basedir
    if not os.isdir(get_xim_install_basedir()) then
        os.mkdir(get_xim_install_basedir())
    end
    -- create xim index repos dir
    if not os.isdir(get_xim_index_reposdir()) then
        os.mkdir(get_xim_index_reposdir())
    end
    -- create xim local index repo dir
    if not os.isdir(get_xim_local_index_repodir()) then
        os.mkdir(get_xim_local_index_repodir())
    end
end

function get_runtime_data()
    return xim_runtime_data
end

function set_runtime_data(data)
    if data.pkginfo then
        set_pkginfo(data.pkginfo)
    end
    if data.rundir then
        xim_runtime_data.rundir = data.rundir
    end
    if data.input_args then
        xim_runtime_data.input_args = data.input_args
    end
end

function get_pkginfo()
    return xim_runtime_data.pkginfo
end

function set_pkginfo(info)
    if info.name ~= nil then
        xim_runtime_data.pkginfo.name = info.name
    end
    if info.version ~= nil then
        xim_runtime_data.pkginfo.version = info.version
    end
    if info.namespace ~= nil then
        xim_runtime_data.pkginfo.namespace = info.namespace
    end
    if info.install_file ~= nil then
        xim_runtime_data.pkginfo.install_file = info.install_file
    end
    if info.install_dir ~= nil then
        xim_runtime_data.pkginfo.install_dir = info.install_dir
    end
    if info.deps_list ~= nil then
        xim_runtime_data.pkginfo.deps_list = info.deps_list
    end
    if info.elfpatch_auto ~= nil then
        xim_runtime_data.pkginfo.elfpatch_auto = info.elfpatch_auto
    end
    if info.elfpatch_shrink ~= nil then
        xim_runtime_data.pkginfo.elfpatch_shrink = info.elfpatch_shrink
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
    if __xim_data_dir == nil then
        local env_data = os.getenv("XLINGS_DATA")
        if env_data and env_data ~= "" then
            __xim_data_dir = env_data
        else
            __xim_data_dir = path.join(platform.get_config_info().homedir, "data")
        end
        if not os.isdir(__xim_data_dir) then
            os.mkdir(__xim_data_dir)
        end
    end
    return __xim_data_dir
end

function get_xim_install_basedir()
    if __xim_install_basedir == nil then
        __xim_install_basedir = path.join(get_xim_data_dir(), "xpkgs")
    end
    return __xim_install_basedir
end

function get_xim_index_reposdir()
    if __xim_index_reposdir == nil then
        -- default xim index repos dir
        __xim_index_reposdir = path.join(get_xim_data_dir(), "xim-index-repos")
    end
    return __xim_index_reposdir
end

function get_xim_local_index_repodir()
    if __xim_local_index_repodir == nil then
        -- default xim local index repo dir
        __xim_local_index_repodir = path.join(get_xim_data_dir(), "local-indexrepo")
    end
    return __xim_local_index_repodir
end

function get_runtime_dir()
    if __xim_runtime_dir == nil then
        -- default xim runtime dir
        __xim_runtime_dir = path.join(get_xim_data_dir(), "runtimedir")
    end
    return __xim_runtime_dir
end

function main()
    print(get_xim_data_dir())
end
