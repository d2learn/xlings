import("common")

local installer_base_dir = path.directory(os.scriptdir())

function load_installers(dir)
    local installers = {}

    local installers_dir = path.join(installer_base_dir, dir)

    for _, file in ipairs(os.files(path.join(installers_dir, "*.lua"))) do
        local name = path.basename(file)
        local installer = import("xim." .. dir .. "." .. name)
        installers[name] = installer
    end

    return installers
end

function deep_copy(orig)
    local copies = {}

    local function _copy(obj)
        if type(obj) ~= "table" then
            return obj
        end

        if copies[obj] then
            return copies[obj]
        end

        local new_table = {}
        copies[obj] = new_table

        for key, value in pairs(obj) do
            new_table[_copy(key)] = _copy(value)
        end

        return debug.setmetatable(new_table, debug.getmetatable(obj))
    end

    return _copy(orig)
end

function os_type()
    local os_type = os.host()
    
    if os_type == "linux" then
        os_type = linuxos.name()
        -- os_version = linuxos.version() -- TODO: get linux version
    end

    return os_type
end

function os_info()
    local os_type = os.host()
    local name, version = "", ""

    if os_type == "linux" then
        name = linuxos.name()
        version = linuxos.version() -- TODO: get linux version
    elseif os_type == "windows" then
        name = "windows" -- winos.name()
        version = winos.version()
    elseif os_type == "macosx" then
        name = macosxos.name()
        version = macos.version()
    end

    return {
        name = name,
        version = version
    }
end

function try_download_and_check(url, dir, sha256)
    local filename = path.join(dir, path.filename(url))
    if not os.isfile(filename) then
        common.xlings_download(url, filename)
    end
    if sha256 then
        if hash.sha256(filename) ~= sha256 then
            os.tryrm(filename)
            return false, filename
        end
    end
    return true, filename
end

function is_compressed(filename)
    local ext = path.extension(filename)
    local exts = {
        ['.zip'] = true,
        ['.tar'] = true,
        ['.gz']  = true,
        ['.bz2'] = true,
        ['.xz']  = true,
        ['.7z']  = true
    }
    return exts[ext] or false
end

function local_package_manager()
    local osinfo = os_info()
    local pm = nil
    if osinfo.name == "windows" then
        pm = "winget"
    elseif osinfo.name == "ubuntu" then
        pm = "apt"
    elseif osinfo.name == "archlinux" then
        pm = "pacman"
    elseif is_host("macosx") then
        -- TODO
    end
    return pm
end

function deref(tlb, key)
    while tlb[key].ref do
        key = tlb[key].ref
    end
    return key, tlb[key] 
end

-- TODO: optmize
function prompt(message, value)
    cprint("${cyan blink}-> ${clear}%s", message)
    local confirm = io.read()
    if confirm ~= value then
        return false
    else
        return true
    end
end

function load_module(fullpath, rootdir)
    -- rootdir/a/b/c.lua -> a.b.c
    local pattern = "^" .. rootdir:gsub("[%(%)%.%%%+%-%*%?%[%]%^%$]", "%%%1") .. "/?"
    local relative_path = fullpath:gsub(pattern, "")
    local path_parts = string.split(relative_path, "/")
    local module_path = table.concat(path_parts, "."):gsub("%.lua$", "")
    return inherit(module_path, {rootdir = rootdir})
end

function main()

end