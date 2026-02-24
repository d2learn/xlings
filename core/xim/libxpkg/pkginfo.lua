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

function deps_list()
    return runtime.get_pkginfo().deps_list or {}
end

local function _ends_with(s, suffix)
    return suffix == "" or s:sub(-#suffix) == suffix
end

local function _resolve_dep_dir_via_xvm(dep_name, dep_version)
    local old_value = xvm.log_tag(false)
    local pinfo = xvm.info(dep_name, dep_version or "")
    xvm.log_tag(old_value)
    if not pinfo or not pinfo["SPath"] or not pinfo["Version"] then
        return nil
    end
    local spath = pinfo["SPath"]
    local pver = pinfo["Version"]
    local strs = string.split(spath, pver)
    if #strs == 2 then
        return path.join(strs[1], pver)
    end
    return nil
end

local function _resolve_dep_dir_via_scan(dep_name, dep_version)
    local xpkgs_base = runtime.get_xim_install_basedir()
    for _, dep_root in ipairs(os.dirs(path.join(xpkgs_base, "*")) or {}) do
        local dirname = path.filename(dep_root)
        local direct_match = dirname == dep_name
        local namespace_match = _ends_with(dirname, "-x-" .. dep_name)
        if direct_match or namespace_match then
            local ver = dep_version
            if not ver then
                local vers = os.dirs(path.join(dep_root, "*"))
                if vers and #vers > 0 then
                    table.sort(vers)
                    ver = path.filename(vers[#vers])
                end
            end
            if ver then
                local install_dir = path.join(dep_root, ver)
                if os.isdir(install_dir) then
                    return install_dir
                end
            end
        end
    end
    return nil
end

function dep_install_dir(dep_name, dep_version)
    local dir = _resolve_dep_dir_via_xvm(dep_name, dep_version)
    if dir then
        return dir
    end
    return _resolve_dep_dir_via_scan(dep_name, dep_version)
end

function install_dir(pkgname, pkgversion)
    if pkgname then
        local dep_dir = dep_install_dir(pkgname, pkgversion)
        if dep_dir then
            return dep_dir
        end
        cprint(
            "[xlings:xim]: ${red}cannot get install dir for %s@%s${clear}",
            tostring(pkgname),
            tostring(pkgversion or "latest")
        )
        return nil
    else
        return runtime.get_pkginfo().install_dir
    end
end

function main()
    print(install_dir("code"))
end