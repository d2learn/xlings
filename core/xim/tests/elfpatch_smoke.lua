import("lib.detect.find_tool")
import("base.runtime")
import("libxpkg.elfpatch")

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

local function _readelf_output(filepath, args)
    local tool = find_tool("readelf")
    _assert(tool ~= nil, "readelf not found")
    local full_args = {}
    for _, a in ipairs(args or {}) do
        table.insert(full_args, a)
    end
    table.insert(full_args, filepath)
    return os.iorunv(tool.program, full_args)
end

local function _verify_loader(filepath)
    local content = _readelf_output(filepath, {"-l"})
    local expect = _loader_path()
    _assert(content:find(expect, 1, true) ~= nil, "loader not patched: " .. filepath)
end

local function _verify_runpath(filepath, keyword)
    local content = _readelf_output(filepath, {"-d"})
    _assert(content:find(keyword, 1, true) ~= nil, "runpath mismatch: " .. filepath)
end

local function _mk_tmp_dir()
    local root = path.join(os.projectdir(), "build", "elfpatch-smoke")
    os.tryrm(root)
    os.mkdir(root)
    return root
end

local function _copy_system_elf(src, dst)
    os.mkdir(path.directory(dst))
    os.cp(src, dst, {force = true})
end

function xpkg_main()
    if not is_host("linux") then
        cprint("[elfpatch-smoke] skip: non-linux host")
        return
    end

    local patchelf_tool = find_tool("patchelf")
    _assert(patchelf_tool ~= nil, "patchelf not found")

    local root = _mk_tmp_dir()

    -- 1) Single ELF + explicit rpath
    local single = path.join(root, "single", "echo")
    _copy_system_elf("/bin/echo", single)
    local r1 = elfpatch.patch_elf_loader_rpath(single, {
        loader = "system",
        rpath = {"/tmp/elfpatch-a", "/tmp/elfpatch-b"},
        strict = true
    })
    _assert(r1.patched == 1, "single elf patch failed")
    _verify_loader(single)
    _verify_runpath(single, "/tmp/elfpatch-a:/tmp/elfpatch-b")

    -- 2) Directory auto scan
    local dir_root = path.join(root, "dirscan")
    local f1 = path.join(dir_root, "bin", "echo")
    local f2 = path.join(dir_root, "bin", "ls")
    _copy_system_elf("/bin/echo", f1)
    _copy_system_elf("/bin/ls", f2)
    local r2 = elfpatch.patch_elf_loader_rpath(dir_root, {
        loader = "system",
        rpath = {"/tmp/elfpatch-dir"},
        include_shared_libs = true,
        recurse = true,
        strict = true
    })
    _assert(r2.patched >= 2, "directory patch did not patch expected files")
    _verify_loader(f1)
    _verify_loader(f2)
    _verify_runpath(f1, "/tmp/elfpatch-dir")
    _verify_runpath(f2, "/tmp/elfpatch-dir")

    -- 3) auto(false): should not patch
    local auto_root = path.join(root, "auto")
    local auto_file = path.join(auto_root, "bin", "echo")
    _copy_system_elf("/bin/echo", auto_file)
    runtime.set_pkginfo({
        install_dir = auto_root,
        deps_list = {},
        elfpatch_auto = false
    })
    local r3 = elfpatch.apply_auto({
        loader = "system",
        rpath = {"/tmp/elfpatch-auto-false"}
    })
    _assert((r3.patched or 0) == 0 and (r3.scanned or 0) == 0, "auto(false) should skip patch")

    -- 4) auto(true): xim-style auto apply
    elfpatch.auto(true)
    local r4 = elfpatch.apply_auto({
        loader = "system",
        rpath = {"/tmp/elfpatch-auto-true"},
        include_shared_libs = true
    })
    _assert((r4.patched or 0) >= 1, "auto(true) should patch files")
    _verify_loader(auto_file)
    _verify_runpath(auto_file, "/tmp/elfpatch-auto-true")

    cprint("[elfpatch-smoke] ${green}PASS${clear}")
end

