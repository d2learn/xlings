import("base.xvm")
import("base.runtime")

function install(xpkg)
    local install_dir = runtime.get_pkginfo().install_dir
    local script_file = path.join(install_dir, xpkg.name .. ".lua")
    --local script_content = io.readfile(xpkg.__path)
    -- replace xpkg_main to main
    --script_content = script_content:replace("xpkg_main", "main")
    --io.writefile(script_file, script_content)
    os.tryrm(script_file)
    os.cp(xpkg.__path, script_file)
    xvm.add(xpkg.name, {
        alias = "xlings script " .. script_file,
        -- TODO: fix xvm's SPATH issue "bindir/alias" - for only alias
        bindir = "TODO-FIX-SPATH-ISSUES",
    })
    return true
end

function uninstall(xpkg)
    xvm.remove(xpkg.name, xpkg.version)
    return true
end