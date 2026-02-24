import("base.xvm")
import("base.runtime")

function install(xpkg)
    local install_dir = runtime.get_pkginfo().install_dir
    local script_file = path.join(install_dir, xpkg.name .. ".lua")
    os.tryrm(script_file)
    os.cp(xpkg.__path, script_file)
    return true
end

function config(xpkg)
    local install_dir = runtime.get_pkginfo().install_dir
    local script_file = path.join(install_dir, xpkg.name .. ".lua")
    xvm.add(xpkg.name, {
        alias = "xlings script " .. script_file,
        bindir = install_dir,
    })
    return true
end

function uninstall(xpkg)
    xvm.remove(xpkg.name, xpkg.version)
    return true
end
