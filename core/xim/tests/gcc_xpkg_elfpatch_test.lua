import("lib.detect.find_tool")

local function _assert(cond, msg)
    if not cond then
        raise(msg or "assert failed")
    end
end

local function _loader_path()
    if os.isfile("/lib64/ld-linux-x86-64.so.2") then
        return "/lib64/ld-linux-x86-64.so.2"
    end
    return "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
end

local function _readelf(filepath, args)
    local tool = find_tool("readelf")
    _assert(tool ~= nil, "readelf not found")
    local argv = {}
    for _, a in ipairs(args or {}) do
        table.insert(argv, a)
    end
    table.insert(argv, filepath)
    return os.iorunv(tool.program, argv)
end

local function _pick_gcc_archive()
    local env_archive = os.getenv("GCC_XPKG_ARCHIVE")
    if env_archive and os.isfile(env_archive) then
        return env_archive
    end

    local home = os.getenv("HOME") or ""
    local candidates = {
        path.join(os.projectdir(), "build", "fixtures", "gcc-15.1.0-linux-x86_64.tar.gz"),
        path.join(os.projectdir(), "build", "fixtures", "gcc-15.1.0-linux.tar.gz"),
        path.join(home, "gcc-15.1.0-linux-x86_64.tar.gz"),
        path.join(home, "gcc-15.1.0-linux.tar.gz"),
    }
    for _, p in ipairs(candidates) do
        if os.isfile(p) then
            return p
        end
    end
    return nil
end

local function _write(pathname, content)
    os.mkdir(path.directory(pathname))
    io.writefile(pathname, content)
end

local function _xlings_data_root()
    return path.join(os.projectdir(), "build", "gcc-xpkg-elfpatch-test", "xlings-data")
end

local function _gcc_pkg_file()
    local env_repo = os.getenv("XIM_PKGINDEX_DIR")
    local candidates = {}
    if env_repo and #env_repo > 0 then
        table.insert(candidates, path.join(env_repo, "pkgs", "g", "gcc.lua"))
    end

    table.insert(candidates, path.join(os.projectdir(), "tests", "fixtures", "xim-pkgindex", "pkgs", "g", "gcc.lua"))
    table.insert(candidates, path.join(os.projectdir(), "..", "xim-pkgindex", "pkgs", "g", "gcc.lua"))
    table.insert(candidates, path.join(os.projectdir(), "..", "d2learn", "xim-pkgindex", "pkgs", "g", "gcc.lua"))
    table.insert(candidates, path.join(os.projectdir(), "..", "..", "xim-pkgindex", "pkgs", "g", "gcc.lua"))
    table.insert(candidates, path.join(os.projectdir(), "..", "..", "d2learn", "xim-pkgindex", "pkgs", "g", "gcc.lua"))

    for _, p in ipairs(candidates) do
        if os.isfile(p) then
            return p
        end
    end
    return nil
end

local function _prepare_stub_modules(sandbox, install_dir, archive)
    local loader = _loader_path()
    local xlings_data = _xlings_data_root()

    _write(path.join(sandbox, "xim", "libxpkg", "pkginfo.lua"), string.format([[
local __install_dir = %q
local __install_file = %q
local __version = "15.1.0"
local __xlings_data = %q

function install_file()
    return __install_file
end

function install_dir(name, version)
    if not name then
        return __install_dir
    end
    -- explicit dep directories used by gcc.lua manual rpath list
    return path.join(__xlings_data, "xpkgs", name, tostring(version or ""))
end

function version()
    return __version
end
]], install_dir, archive, xlings_data))

    _write(path.join(sandbox, "xim", "libxpkg", "log.lua"), [[
function info(...) end
function warn(...) end
function error(...) end
]])

    _write(path.join(sandbox, "xim", "libxpkg", "xvm.lua"), [[
function add(...) return true end
function remove(...) return true end
]])

    _write(path.join(sandbox, "xim", "libxpkg", "pkgmanager.lua"), [[
function install(...) return true end
function uninstall(...) return true end
]])

    _write(path.join(sandbox, "xim", "libxpkg", "elfpatch.lua"), string.format([[
local __auto = false
local __loader = %q
local __patchelf = "patchelf"

local function __rpath(v)
    if type(v) == "string" then return v end
    if type(v) ~= "table" then return nil end
    return table.concat(v, ":")
end

local function __targets(target, recurse)
    if os.isfile(target) then return {target} end
    if not os.isdir(target) then return {} end
    local pat = recurse == false and path.join(target, "*") or path.join(target, "**")
    return os.files(pat) or {}
end

function auto(v)
    __auto = (v == true)
    return __auto
end

function closure_lib_paths()
    return {}
end

function patch_elf_loader_rpath(target, opts)
    opts = opts or {}
    local rp = __rpath(opts.rpath)
    local files = __targets(target, opts.recurse)
    local patched = 0
    for _, f in ipairs(files) do
        if os.isfile(f) then
            local is_elf = false
            local hdr = try {
                function()
                    return os.iorun("readelf -h " .. f)
                end,
                catch { function() return nil end }
            }
            if type(hdr) == "string" and hdr:find("ELF Header", 1, true) then
                is_elf = true
            end
            if not is_elf then
                goto continue
            end
            local ok = try {
                function()
                    os.exec(__patchelf .. " --set-interpreter " .. __loader .. " " .. f)
                    if rp and rp ~= "" then
                        os.exec(__patchelf .. " --set-rpath " .. rp .. " " .. f)
                    end
                    return true
                end,
                catch { function() return false end }
            }
            if ok then patched = patched + 1 end
        end
        ::continue::
    end
    return {scanned = #files, patched = patched, failed = (#files - patched)}
end
]], loader))
end

function xpkg_main()
    if not is_host("linux") then
        cprint("[gcc-xpkg-elfpatch-test] skip non-linux")
        return
    end

    local archive = _pick_gcc_archive()
    _assert(archive ~= nil, "gcc prebuilt archive not found; set GCC_XPKG_ARCHIVE or put fixture under build/fixtures")

    local test_root = path.join(os.projectdir(), "build", "gcc-xpkg-elfpatch-test")
    local install_dir = path.join(test_root, "gcc", "15.1.0")
    local sandbox = path.join(test_root, "sandbox")
    os.tryrm(test_root)
    os.mkdir(test_root)

    _prepare_stub_modules(sandbox, install_dir, archive)
    local gcc_pkg_file = _gcc_pkg_file()
    _assert(gcc_pkg_file ~= nil, "gcc.lua not found; set XIM_PKGINDEX_DIR or keep xim-pkgindex adjacent to this repo")
    os.cp(gcc_pkg_file, path.join(sandbox, "gcc.lua"), {force = true, symlink = false})

    local gcc_pkg = import("gcc", {rootdir = sandbox, anonymous = true})
    _assert(gcc_pkg.install(), "gcc xpkg install hook failed")

    local gcc_bin = path.join(install_dir, "bin", "gcc")
    _assert(os.isfile(gcc_bin), "gcc binary missing after install hook")

    local interp = _readelf(gcc_bin, {"-l"})
    _assert(interp:find(_loader_path(), 1, true) ~= nil, "gcc loader not patched to system loader")

    local dyn = _readelf(gcc_bin, {"-d"})
    _assert(dyn:find(path.join(install_dir, "lib64"), 1, true) ~= nil, "gcc rpath missing install_dir/lib64")

    local candidates = os.files(path.join(install_dir, "**", "cc1plus")) or {}
    if #candidates > 0 then
        local interp2 = _readelf(candidates[1], {"-l"})
        _assert(interp2:find(_loader_path(), 1, true) ~= nil, "cc1plus loader not patched by directory scan")
    end

    cprint("[gcc-xpkg-elfpatch-test] ${green}PASS${clear}")
end
