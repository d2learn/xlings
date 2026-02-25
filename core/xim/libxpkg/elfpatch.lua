import("base.runtime")
import("lib.detect.find_tool")
import("index.IndexManager")
import("pm.XPackage")
import("libxpkg.log")
import("libxpkg.pkginfo")

local _index_manager = nil
local _patchelf = nil
local _readelf = nil

local function _trim(s)
    if not s then
        return s
    end
    return s:match("^%s*(.-)%s*$")
end

local function _index()
    if not _index_manager then
        _index_manager = IndexManager.new()
        _index_manager:init()
    end
    return _index_manager
end

local function _get_tool(cache_key, toolname)
    if cache_key == "patchelf" then
        if _patchelf == nil then
            _patchelf = find_tool(toolname)
        end
        return _patchelf
    end
    if cache_key == "readelf" then
        if _readelf == nil then
            _readelf = find_tool(toolname)
        end
        return _readelf
    end
    return nil
end

local function _is_elf(filepath)
    local readelf = _get_tool("readelf", "readelf")
    if not readelf then
        -- Fallback to "best effort" when readelf is unavailable.
        return true
    end
    return try {
        function()
            os.iorunv(readelf.program, {"-h", filepath})
            return true
        end,
        catch {
            function()
                return false
            end
        }
    }
end

local function _normalize_rpath(rpath)
    if not rpath then
        return nil
    end
    if type(rpath) == "string" then
        return rpath
    end
    if type(rpath) ~= "table" then
        return nil
    end

    local seen = {}
    local values = {}
    for _, p in ipairs(rpath) do
        if p and p ~= "" and not seen[p] then
            seen[p] = true
            table.insert(values, p)
        end
    end
    if #values == 0 then
        return nil
    end
    return table.concat(values, ":")
end

local function _detect_system_loader()
    local candidates = {
        "/lib64/ld-linux-x86-64.so.2",
        "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
        "/lib/ld-musl-x86_64.so.1",
    }
    for _, p in ipairs(candidates) do
        if os.isfile(p) then
            return p
        end
    end

    local readelf = _get_tool("readelf", "readelf")
    if readelf and os.isfile("/bin/sh") then
        local output = try {
            function()
                return os.iorunv(readelf.program, {"-l", "/bin/sh"})
            end,
            catch {
                function()
                    return nil
                end
            }
        }
        if output then
            local loader = _trim(output:match("Requesting program interpreter:%s*([^%]]+)"))
            if loader and os.isfile(loader) then
                return loader
            end
        end
    end
    return nil
end

local function _resolve_loader(loader_opt)
    if not loader_opt then
        return nil
    end
    if loader_opt == "system" then
        return _detect_system_loader()
    end
    if loader_opt == "subos" then
        local cfg = import("platform").get_config_info()
        if cfg and cfg.subosdir then
            local root = cfg.subosdir
            for _, p in ipairs({
                path.join(root, "lib", "ld-linux-x86-64.so.2"),
                path.join(root, "lib64", "ld-linux-x86-64.so.2"),
                path.join(root, "lib", "ld-musl-x86_64.so.1"),
            }) do
                if os.isfile(p) then
                    return p
                end
            end
        end
        return nil
    end
    return loader_opt
end

local function _collect_targets(target, opts)
    if not target then
        return {}
    end
    if os.isfile(target) then
        return {target}
    end
    if not os.isdir(target) then
        return {}
    end

    opts = opts or {}
    local recurse = opts.recurse
    if recurse == nil then
        recurse = true
    end
    local include_shared_libs = opts.include_shared_libs
    if include_shared_libs == nil then
        include_shared_libs = true
    end

    local pattern = recurse and path.join(target, "**") or path.join(target, "*")
    local files = os.files(pattern) or {}
    local elfs = {}
    for _, f in ipairs(files) do
        if include_shared_libs then
            if _is_elf(f) then
                table.insert(elfs, f)
            end
        else
            -- exclude obvious shared library names if caller only wants executables
            local is_shared = f:find("%.so", 1, true) ~= nil
            if not is_shared and _is_elf(f) then
                table.insert(elfs, f)
            end
        end
    end
    return elfs
end

function auto(enable_or_opts)
    local patch_auto = nil
    local patch_shrink = nil

    if type(enable_or_opts) == "table" then
        if enable_or_opts.enable ~= nil then
            patch_auto = (enable_or_opts.enable == true)
        end
        if enable_or_opts.shrink ~= nil then
            patch_shrink = (enable_or_opts.shrink == true)
        end
    else
        patch_auto = (enable_or_opts == true)
    end

    local update = {}
    if patch_auto ~= nil then
        update.elfpatch_auto = patch_auto
    end
    if patch_shrink ~= nil then
        update.elfpatch_shrink = patch_shrink
    end
    runtime.set_pkginfo(update)
    return runtime.get_pkginfo().elfpatch_auto
end

function is_auto()
    return runtime.get_pkginfo().elfpatch_auto == true
end

function is_shrink()
    return runtime.get_pkginfo().elfpatch_shrink == true
end

local function _resolve_dep_install_dir(dep_spec)
    local dep_name = dep_spec and dep_spec:gsub("@.*", "") or ""
    dep_name = dep_name:gsub("^.+:", "")
    local dep_version = (dep_spec and dep_spec:find("@", 1, true)) and dep_spec:match("@(.+)") or nil

    local index = _index()
    local matched = dep_spec
    local pkg_meta = index:load_package(matched)
    if not pkg_meta then
        matched = index:match_package_version(dep_spec)
        if not matched then
            return pkginfo.dep_install_dir(dep_name, dep_version)
        end
        pkg_meta = index:load_package(matched)
    end

    if not pkg_meta then
        return pkginfo.dep_install_dir(dep_name, dep_version)
    end

    local xpkg = try {
        function()
            return XPackage.new(pkg_meta)
        end,
        catch {
            function()
                return nil
            end
        }
    }
    if not xpkg then
        return pkginfo.dep_install_dir(dep_name, dep_version)
    end
    local effective_name = xpkg.name
    if xpkg.namespace then
        effective_name = xpkg.namespace .. "-x-" .. xpkg.name
    end
    local install_dir = path.join(runtime.get_xim_install_basedir(), effective_name, xpkg.version)
    if os.isdir(install_dir) then
        return install_dir
    end
    return pkginfo.dep_install_dir(dep_name, dep_version)
end

function closure_lib_paths(opt)
    opt = opt or {}
    local resolved_deps_list = opt.resolved_deps_list or runtime.get_pkginfo().resolved_deps_list or {}
    local deps_list = opt.deps_list or runtime.get_pkginfo().deps_list or {}
    local dep_specs = resolved_deps_list
    if #dep_specs == 0 then
        dep_specs = deps_list
    end
    local values = {}
    local seen = {}

    local pkg = runtime.get_pkginfo()
    if pkg and pkg.install_dir then
        for _, sub in ipairs({"lib64", "lib"}) do
            local self_libdir = path.join(pkg.install_dir, sub)
            if os.isdir(self_libdir) and not seen[self_libdir] then
                seen[self_libdir] = true
                table.insert(values, self_libdir)
                break
            end
        end
    end

    for _, dep_spec in ipairs(dep_specs) do
        local dep_dir = _resolve_dep_install_dir(dep_spec)
        if dep_dir then
            for _, sub in ipairs({"lib64", "lib"}) do
                local libdir = path.join(dep_dir, sub)
                if os.isdir(libdir) and not seen[libdir] then
                    seen[libdir] = true
                    table.insert(values, libdir)
                    break
                end
            end
        end
    end

    local cfg = import("platform").get_config_info()
    if cfg and cfg.subosdir then
        local subos_lib = path.join(cfg.subosdir, "lib")
        if not seen[subos_lib] then
            seen[subos_lib] = true
            table.insert(values, subos_lib)
        end
    end

    return values
end

function patch_elf_loader_rpath(target, opts)
    opts = opts or {}
    local result = {
        scanned = 0,
        patched = 0,
        failed = 0,
        shrinked = 0,
        shrink_failed = 0
    }

    if not is_host("linux") then
        return result
    end

    local patch_tool = _get_tool("patchelf", "patchelf")
    if not patch_tool then
        log.warn("patchelf not found, skip patching")
        return result
    end

    local loader = _resolve_loader(opts.loader)
    local rpath = _normalize_rpath(opts.rpath)
    if opts.loader and not loader then
        local msg = string.format("cannot resolve loader from option: %s", tostring(opts.loader))
        if opts.strict then
            raise(msg)
        end
        log.warn(msg)
    end

    local targets = _collect_targets(target, opts)
    for _, filepath in ipairs(targets) do
        result.scanned = result.scanned + 1

        local ok = try {
            function()
                if loader then
                    os.vrunv(patch_tool.program, {"--set-interpreter", loader, filepath})
                end
                if rpath and rpath ~= "" then
                    os.vrunv(patch_tool.program, {"--set-rpath", rpath, filepath})
                end
                return true
            end,
            catch {
                function(e)
                    if opts.strict then
                        raise(e)
                    end
                    return false
                end
            }
        }

        if ok then
            log.info("patched: %s", filepath)
            if opts.shrink == true then
                local shrink_ok = try {
                    function()
                        os.vrunv(patch_tool.program, {"--shrink-rpath", filepath})
                        return true
                    end,
                    catch {
                        function(e)
                            if opts.strict then
                                raise(e)
                            end
                            return false
                        end
                    }
                }
                if shrink_ok then
                    result.shrinked = result.shrinked + 1
                else
                    result.shrink_failed = result.shrink_failed + 1
                end
            end
            result.patched = result.patched + 1
        else
            result.failed = result.failed + 1
        end
    end

    return result
end

function apply_auto(opts)
    opts = opts or {}
    if not is_auto() then
        return {scanned = 0, patched = 0, failed = 0}
    end
    local pkg = runtime.get_pkginfo()
    local target = opts.target or pkg.install_dir
    local rpath = opts.rpath or closure_lib_paths({
        deps_list = pkg.deps_list,
        resolved_deps_list = pkg.resolved_deps_list
    })
    local shrink = opts.shrink
    if shrink == nil then
        shrink = is_shrink()
    end
    return patch_elf_loader_rpath(target, {
        loader = opts.loader or "subos",
        rpath = rpath,
        shrink = shrink,
        include_shared_libs = opts.include_shared_libs,
        recurse = opts.recurse,
        strict = opts.strict
    })
end
