-- Package IndexManager

import("xim.base.utils")
import("xim.index.RepoManager")
import("xim.index.IndexStore")

local IndexManager = {}
IndexManager.__index = IndexManager
IndexManager.status_changed_pkg = {}

local index_store = nil

function new()
    -- singleton
    if IndexManager.index == nil then
        IndexManager:init()
    end
    return IndexManager
end

function IndexManager:init()
    if index_store == nil then
        local repo_manager = RepoManager.new()
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

function IndexManager:rebuild()
    index_store:rebuild()
    self.index = index_store:get_index_data()
end

function IndexManager:search(query, limit)
    local names = {}

    query = query or ""
    limit = limit or 1000

    for k, v in pairs(self.index) do
        if k:find(query) or query == "" then
            if not v.ref then
                table.insert(names, k)
                if #names >= limit then
                    break
                end
            end
        end
    end

    return names
end

function IndexManager:load_package(name)
    if self.index[name] then

        local _, pkg = utils.deref(self.index, name)

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

function main()
    local index_manager = new()
    index_manager.status_changed_pkg["vscode"] = {installed = false}
    index_manager:update()
end