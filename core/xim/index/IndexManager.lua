-- Package IndexManager

import("xim.base.utils")
import("xim.index.RepoManager")
import("xim.index.IndexStore")

local IndexManager = {}
IndexManager.__index = IndexManager
IndexManager.status_changed_pkg = {}

local index_store = nil
local repo_manager = nil

function new()
    -- singleton
    return IndexManager
end

function IndexManager:init()
    if index_store == nil then
        repo_manager = RepoManager.new()
        index_store = IndexStore.new(repo_manager:repodirs())
        self.index = index_store:get_index_data()
    end
end

function IndexManager:update()
    for name, status in pairs(IndexManager.status_changed_pkg) do
        index_store:update_index_data(name, status)
    end
    index_store:save_to_local()
    IndexManager.status_changed_pkg = {}
end

function IndexManager:sync_repo()
    repo_manager:sync()
end

function IndexManager:rebuild()
    index_store:rebuild()
    self.index = index_store:get_index_data()
end

function IndexManager:search(query, opt)
    local names = {
        -- ["name"] = { alias1, alias2, ... }
    }

    opt = opt or {}
    query = query or ""
    opt.limit = opt.limit or 1000

    function is_installed(opt, pkg)
        return opt.installed == nil or opt.installed == pkg.installed
    end

    function add_name(names, name, alias)
        if names[name] == nil then
            names[name] = {}
        end
        if alias then
            if #names[name] < 5 then
                table.insert(names[name], alias)
            else
                names[name][5] = "..."
            end
        end
    end

    for name, pkg in pairs(self.index) do
        if name:find(query, 1, true) or query == "" then
            local alias_name = nil
            if pkg.ref then
                alias_name = name
                name, pkg = utils.deref(self.index, pkg.ref)
            end
            -- if ref to a nil, skip
            if name and is_installed(opt, pkg) then 
                add_name(names, name, alias_name)
                if #names >= opt.limit then
                    break
                end
            end
        end
    end
    return names
end

function IndexManager:match_package_version(target)

    local target_match_str = target

    if not target:find("@", 1, true) then
        target_match_str = target .. "@"
    end

    local target_versions = {}
    for name, pkg in pairs(self.index) do
        if name:find(target_match_str, 1, true) == 1 then
            if pkg.installed then return name end
            table.insert(target_versions, name)
        end
    end

    if self.index[target] then
        -- not found in installed packages
        return target -- exact match to default(latest) version
    elseif #target_versions > 0 then
        -- TODO: sort by version number
        table.sort(target_versions)
        return target_versions[1]
    end

    return nil
end

function IndexManager:load_package(name)
    if self.index[name] then

        local _, pkg = utils.deref(self.index, name)

        -- TODO: fix index-database ref nil value issue
        --       IndexStore.lua:build_xpkg_index
        --       add ref-platform check when add to index-db
        if pkg == nil then
            return nil
        end

        if pkg.pmwrapper then
            return pkg
        end

        -- package cache
        if pkg.data == nil then
            -- load package file
            -- print("load package file: " .. pkg.path)
            -- replace the package index value with the package file content
            local data = inherit(
                path.basename(pkg.path),
                {rootdir = path.directory(pkg.path)}
            )
            -- pkg is a reference to the package
            -- auto update self.index[ref].data
            --self.index[self.index[name].ref].data = data
            pkg.data = data
        end
        return pkg
    else
        cprint("[xlings:xim]: ${dim yellow}load package data failed - ${green}%s", name)
        return nil
    end
end

function IndexManager:add_xpkg(xpkg_file)
    cprint("[xlings:xim]: add xpkg - ${clear}%s", xpkg_file)
    if index_store:build_xpkg_index(xpkg_file) then
        index_store:save_to_local(true)
    end
end

function main()
    local index_manager = new()
    index_manager.status_changed_pkg["vscode"] = {installed = false}
    index_manager:update()
end