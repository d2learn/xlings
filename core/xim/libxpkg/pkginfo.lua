import("xim.base.runtime")

function name()
    return runtime.get_pkginfo().name
end

function version()
    return runtime.get_pkginfo().version
end

function install_file()
    return runtime.get_pkginfo().install_file
end

function install_dir()
    return runtime.get_pkginfo().install_dir
end