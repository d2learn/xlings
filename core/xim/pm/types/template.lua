import("base.xvm")
import("base.runtime")

function installed(xpkg)
    local old_value = xvm.log_tag(false)
    ret = xvm.has(__get_xvm_pkgname(xpkg), xpkg.version)
    xvm.log_tag(old_value)
    return ret
end

function install(xpkg)
    local pkginfo = runtime.get_pkginfo()
    local install_file = pkginfo.install_file

    pkgdir = install_file
        :replace(".zip", "")
        :replace(".tar.gz", "")
        :replace(".git", "")

    os.tryrm(pkginfo.install_dir)

    if not os.trymv(pkgdir, pkginfo.install_dir) then
        cprint("[xlings:xim:template]: ${red bright}failed to install to %s${clear}", pkginfo.install_dir)
        return false
    end

    xvm.add(__get_xvm_pkgname(xpkg))

    return true
end

function uninstall(xpkg)
    xvm.remove(__get_xvm_pkgname(xpkg), xpkg.version)
    return true
end

--private

function __get_xvm_pkgname(xpkg)
    local pkgname = xpkg.name
    local namespace = xpkg.namespace
    local xvm_pkgname = ""

    if namespace and namespace ~= "" then
        xvm_pkgname = namespace .. "-x-" .. pkgname
    else
        xvm_pkgname = "template-x-" .. pkgname
    end

    return xvm_pkgname
end