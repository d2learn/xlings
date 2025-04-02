import("xim.base.utils")
import("xim.base.runtime")

local IndexStore = {}
IndexStore.__index = IndexStore

local index_db_file = path.join(runtime.get_xim_data_dir(), "xim-index-db.lua")
local default_repo_dir = {
    path.join(path.directory(os.scriptdir()), "pkgindex")
}

function new(indexdirs)
    local instance = {}
    debug.setmetatable(instance, IndexStore)

    instance.main_repodir = indexdirs["main"]
    instance.subrepos = indexdirs["subrepos"] or { }

    instance._index_data = { }
    instance._pkg_reflist = { }
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
    self._pkg_reflist = {
        -- [package_name] = pkg.ref
    }

    --os.tryrm(index_db_file)
    -- avoid re-create index_db_file and permission changed
    _save_index_data(self._index_data)

    -- rebuild main repo
    self:rebuild_from_indexdir(self.main_repodir)

    -- rebuild subrepos
    for namespace, indexdir in pairs(self.subrepos) do
        self:rebuild_from_indexdir(indexdir, namespace)
    end

    -- merge pkg_reflist to index_data
    for pkg_name, pkg_ref in pairs(self._pkg_reflist) do
        -- if pkg_ref is valid
        if self._index_data[pkg_ref] then
            self._index_data[pkg_name] = {
                ref = pkg_ref
            }
        else
            cprint("[xlings:xim]: ${yellow}xpackage ref-[%s] invalid, skip - ${clear}%s", pkg_ref, pkg_name)
        end
    end

    _save_index_data(self._index_data)
    return self._index_data
end

function IndexStore:rebuild_from_indexdir(indexdir, namespace)

    -- init namespace
    self._namespace = namespace

    cprint("[xlings:xim]: rebuild index for [%s] namespace", namespace or "main")

    local pkgindex_build_file = path.join(indexdir, "pkgindex-build.lua")
    if os.isfile(pkgindex_build_file) then
        cprint("[xlings:xim]: local pkgindex-build file found, start building...")
        local pkgindex_build = utils.load_module(
            pkgindex_build_file,
            indexdir
        )
        pkgindex_build.install()
    end

    self:build_xpkgs_index(indexdir)
    self:build_pmwrapper_index(indexdir)

    -- set to nil to avoid side effect
    self._namespace = nil
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
            local package_name = path.basename(xpkg_file)

            local pkg = utils.load_module(
                xpkg_file,
                path.directory(xpkg_file)
            ).package

            if pkg.namespace then
                package_name = pkg.namespace .. ":" .. package_name
            elseif self._namespace then
                package_name = self._namespace .. ":" .. package_name
            end

            -- TODO: package_name@version@arch
            if pkg.ref then
                if self._index_data[pkg.ref] then
                    self._index_data[package_name] = {
                        ref = pkg.ref
                    }
                else
                    -- check/merge to indexdata after all index data generated
                    self._pkg_reflist[package_name] = pkg.ref
                end
            else
                local target_os = utils.xpm_target_os_helper(pkg.xpm)
                -- if target_os is nil, skip this package
                if target_os then
                    local os_key = pkg.xpm[target_os].ref or target_os
                    for version, _ in pairs(pkg.xpm[os_key]) do

                        if version ~= "deps" then
                            local key = string.format("%s@%s", package_name, version)

                            if pkg.xpm[os_key][version].ref then
                                self._index_data[key] = {
                                    ref = package_name .. "@" .. pkg.xpm[os_key][version].ref
                                }
                            else 
                                self._index_data[key] = {
                                    version = version,
                                    installed = false,
                                    path = xpkg_file
                                }
                            end

                            if version == "latest" then
                                self._index_data[package_name] = {
                                    ref = key
                                }
                            end
                        end
                    end -- for end
                end -- if target_os
            end
            return true
        end,
        catch {
            function(e)
                cprint("\n${red}%s${clear}\n", e)
                cprint("[xlings:xim]: ${yellow}xpackage file error or nil, skip - ${clear}%s", xpkg_file)
                return false
            end
        }
    }
end

function IndexStore:build_pmwrapper_index(indexdir)
    local os_info = utils.os_info()
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
                    key = string.format("%s@%s", name, version)
                    if self._namespace then
                        key = self._namespace .. ":" .. key
                    end
                    self._index_data[key] = {
                        ref = target_key .. "@" .. version
                    }
                end
            elseif pm[os_info.name] then
                local version = pm[os_info.name][1]
                local pkgname = pm[os_info.name][2]
                key = string.format("%s@%s", name, version)
                if self._namespace then
                    key = self._namespace .. ":" .. key
                end
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