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
    local name, version, upstream = "", "unknown-todo", "unknown-todo"

    if os_type == "linux" then
        name = linuxos.name()
        try {
            function() version = linuxos.version() end,
            catch { 
                function(e)
                    -- TODO: fix xmake's linux version issue
                end 
            }
        }
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
    elseif osinfo.name == "archlinux" or osinfo.name == "manjaro" then
        pm = "pacman"
    elseif is_host("macosx") then
        -- TODO
    end
    return pm
end

function deref(tlb, key)

    if tlb[key] == nil then return nil end

    while tlb[key].ref do
        key = tlb[key].ref
        if tlb[key] == nil then
            return nil
        end
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

function add_env_path(value)

    local old_value = os.getenv("PATH")

    if string.find(old_value, value, 1, true) then
        cprint("[xlings:xim]: env-path [%s] already exists", value)
        return
    end

    cprint("[xlings:xim]: add [%s] to env-path", value)
    os.addenv("PATH", value)

    if is_host("windows") then
        common.xlings_exec("setx PATH \"" .. value .. ";%PATH%\"")
    else
        local content = string.format("export PATH=%s:$PATH", value)
        append_bashrc(content)
    end
end

function append_bashrc(content)
    local bashrc = os.getenv("HOME") .. "/.bashrc"
    if not os.isfile(bashrc) then
        common.xlings_create_file_and_write(bashrc, content)
    else
        local bashrc_content = io.readfile(bashrc)
        if string.find(bashrc_content, content, 1, true) == nil then
            content = "\n" .. content
            common.xlings_file_append(bashrc, content)
        end
    end
end

function main()

end
