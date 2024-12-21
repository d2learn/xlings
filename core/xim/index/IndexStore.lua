import("xim.base.utils")
import("xim.base.runtime")

local IndexStore = {}
IndexStore.__index = IndexStore

local os_info = utils.os_info()
local index_db_file = path.join(runtime.get_xim_data_dir(), "xim-index-db.lua")
local default_repo_dir = {
    path.join(path.directory(os.scriptdir()), "pkgindex")
}

function new(indexdirs)
    local instance = {}
    debug.setmetatable(instance, IndexStore)
    instance.indexdirs = indexdirs or default_repo_dir
    instance:init()
    return instance
end

function IndexStore:init()
    if os.isfile(index_db_file) then
        try {
            function()
                self._index_data = _load_index_data()
            end,
            catch {
                function(e)
                    print(e)
                    self:rebuild()
                end
            }
        }
    else
        self:rebuild()
    end
end

function IndexStore:rebuild()
    cprint("[xlings:xim]: rebuild index database")
    self._index_data = { }
    os.tryrm(index_db_file)
    for _, indexdir in ipairs(self.indexdirs) do
        self:build_xpkgs_index(indexdir)
        self:build_pmwrapper_index(indexdir)
    end
    _save_index_data(self._index_data)
    return self._index_data
end

function IndexStore:build_xpkgs_index(indexdir)
    local pkgsdir = path.join(indexdir, "pkgs")
    local files = os.files(path.join(pkgsdir, "**.lua"))
    for _, file in ipairs(files) do
        self:build_xpkg_index(file)
    end
end

function IndexStore:build_xpkg_index(xpkg_file)
    return try {
        function()
            local name_maintainer = path.basename(xpkg_file)
            local pkg = utils.load_module(
                xpkg_file,
                path.directory(xpkg_file)
            ).package

            -- TODO: name_maintainer@version@arch
            if pkg.ref then
                self._index_data[name_maintainer] = {
                    ref = pkg.ref
                }
            elseif pkg.xpm[os_info.name] then
                local os_key = pkg.xpm[os_info.name].ref or os_info.name
                for version, _ in pairs(pkg.xpm[os_key]) do

                    if version ~= "deps" then
                        local key = string.format("%s@%s", name_maintainer, version)

                        if pkg.xpm[os_key][version].ref then
                            self._index_data[key] = {
                                ref = name_maintainer .. "@" .. pkg.xpm[os_key][version].ref
                            }
                        else 
                            self._index_data[key] = {
                                version = version,
                                installed = false,
                                path = xpkg_file
                            }
                        end

                        if version == "latest" then
                            self._index_data[name_maintainer] = {
                                ref = key
                            }
                        end
                    end
                end
            end
            return true
        end,
        catch {
            function(e)
                cprint("[xlings:xim]: ${yellow}xpackage file error or nil, skip - ${clear}%s", xpkg_file)
                return false
            end
        }
    }
end

function IndexStore:build_pmwrapper_index(indexdir)
    local pmwrapper_file = path.join(indexdir, "pmwrapper.lua")
    if os.isfile(pmwrapper_file) then
        local pmwrapper = utils.load_module(
            pmwrapper_file,
            indexdir
        ).pmwrapper
        for name, pm in pairs(pmwrapper) do
            local key = nil
            if pm.ref then
                target_key, target_pm = utils.deref(pmwrapper, pm.ref)
                if target_pm[os_info.name] then
                    local version = target_pm[os_info.name][1]
                    key = name .. "@" .. version
                    self._index_data[key] = {
                        ref = target_key .. "@" .. version
                    }
                end
            elseif pm[os_info.name] then
                local version = pm[os_info.name][1]
                local pkgname = pm[os_info.name][2]
                key = string.format("%s@%s", name, version)
                self._index_data[key] = {
                    pmwrapper = version,
                    name = pkgname,
                    installed = false,
                }
            end
            -- (key)os support and not exist xpm index
            if key and self._index_data[name] == nil then
                self._index_data[name] = { ref = key }
            end
        end
    end
end

function IndexStore:save_to_local(force_save)
    if self._need_update or force_save then
        self._need_update = false
        _save_index_data(self._index_data)
    end
end

-- getter/setter

function IndexStore:get_index_data()
    -- create deep copy of index data
    return utils.deep_copy(self._index_data)
end

function IndexStore:update_index_data(name, status)
    status = status or {}
    if status.installed ~= nil then
        name, _ = utils.deref(self._index_data, name)
        if self._index_data[name].installed ~= status.installed then
            self._index_data[name].installed = status.installed
            self._need_update = true
        end
    end
end

----------------------------------------

function _load_index_data()
    local index = {}
    if os.isfile(index_db_file) then
        index = import(
            path.basename(index_db_file),
            {rootdir = path.directory(index_db_file)}
        ).get_index_db()
    end
    return index
end

--[[ TODO: index structure - only support string key
{
    ["name1"] = { path = "name1 package file path", installed = true },
    ["name2"] = { path = "name2 package file path", installed = false}
}
]]

function _save_index_data(index)
    print("[xlings:xim]: update index database")

    local header = string.format([[
--  Generated by xim-index-manager
--  Repo: https://github.com/d2learn/xlings
--  Time: %s
--  DO NOT MODIFY THIS FILE MANUALLY!

]], os.date("%Y-%m-%d %H:%M:%S"))

    local data = string.format("xim_index_db = %s", _serialize(index))
    index_interface = [[


function get_index_db()
    return xim_index_db
end

    ]]
    data = header .. data .. index_interface
    io.writefile(index_db_file, data)
end

function _serialize(obj, layer)
    layer = layer or 1
    local indent = string.rep("    ", layer)
    local s = ""
    if type(obj) == "table" then
        s = "{"
        for k, v in pairs(obj) do
            s = s .. string.format("\n%s[%s] = %s,",
                indent,
                _serialize(k),
                _serialize(v, layer + 1)
            )
        end
        s = s .. "\n" .. string.rep("    ", layer - 1) .. "}"
    elseif type(obj) == "string" then
        s = string.format("%q", obj)
    elseif type(obj) == boolean then
        s = tostring(obj)
    else
        s = tostring(obj)
    end
    return s
end

function main()
    local pkg_index_repo = path.join(path.directory(os.scriptdir()), "pkgindex")
    local index_store = new(pkg_index_repo)
    local index = {}
    index = index_store._index_data
    index["IndexStore"] = "你好"
    print(index)
    print(index_store._index_data)
    index_store:init()
    print(index)
    print(index_store._index_data)
end