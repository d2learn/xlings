import("base.runtime")
import("base.xvm")

function name()
    return runtime.get_pkginfo().name
end

function version()
    return runtime.get_pkginfo().version
end

function install_file()
    return runtime.get_pkginfo().install_file
end

function install_dir(pkgname, pkgversion)
    if pkgname then
        local pinfo = xvm.info(pkgname, pkgversion or "")
        local spath = pinfo["SPath"]

        if not spath then
            cprint(
                "[xlings:xim]: ${red}cannot get install dir: not installed or pkgname(%s) version(%s) exist error",
                tostring(pkgname), tostring(pkgversion)
            )
            return nil
        end

        pkgversion = pinfo["Version"]
        -- split spath by  pkgversion
        local strs = string.split(spath, pkgversion)
        if #strs ~= 2 then
            cprint("[xlings:xim]: ${red}cannot get install dir for %s@%s${clear}", tostring(pkgname), tostring(pkgversion))
            return nil
        else
            return path.join(strs[1], pkgversion)
        end
    else
        return runtime.get_pkginfo().install_dir
    end
end

function main()
    print(install_dir("code"))
end