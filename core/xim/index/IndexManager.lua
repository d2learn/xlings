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
        self.index, self.__mutex_group = index_store:get_index_data()
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
    self.index, self.__mutex_group = index_store:get_index_data()
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

    -- if target is a pkgname-ref, use the ref name
    -- Note: it is different for version-ref "code = { ref = "code@latest" }"
    local pkgnames = target_match_str:split("@", {plain = true})
    -- #pkgnames >= 1
    if self.index[pkgnames[1]] then
        local pkgname_ref = self.index[pkgnames[1]].ref
        if pkgname_ref and not pkgname_ref:find("@", 1, true) then
            target_match_str = pkgname_ref .. "@" .. (pkgnames[2] or "")
        end
    end

    local installed_target_versions = {}
    local nonamespace_target_versions = {}
    local target_versions = {}
    local scode_namespace_versions = {}
    for name, pkg in pairs(self.index) do
        local match_index = name:find(target_match_str, 1, true)
        if match_index == 1 then -- xxxx@ matched
            if pkg.installed then table.insert(installed_target_versions, name)  end
            table.insert(nonamespace_target_versions, name)
        elseif match_index and string.sub(name, match_index - 1, match_index - 1) == ":" then
            -- ???:xxxx@ matched
            if pkg.installed then table.insert(installed_target_versions, name)  end
            if string.find(name, "scode:", 1, true) == 1 then
                -- special case for scode:xxxx@, put it to scode namespace list
                table.insert(scode_namespace_versions, name)
            else
                table.insert(target_versions, name)
            end
        end
    end

    if #installed_target_versions > 0 then
        -- sort by version number, installed packages first
        table.sort(installed_target_versions)
        return installed_target_versions[1]
    elseif self.index[target] then
        -- not found in installed packages
        return target -- exact match to default(latest) version
    elseif #nonamespace_target_versions > 0 then
        table.sort(nonamespace_target_versions)
        return nonamespace_target_versions[1]
    elseif #target_versions > 0 then
        -- TODO: sort by version number
        table.sort(target_versions)
        return target_versions[1]
    elseif #scode_namespace_versions > 0 then
        table.sort(scode_namespace_versions)
        return scode_namespace_versions[1]
    end

    return nil
end

function IndexManager:load_package(name)
    if self.index[name] then

        local name, pkg = utils.deref(self.index, name)

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

            -- add index-repo namespace if not exists
            if not data.package.namespace then
                -- namespace is the part before ':', e.g. namespace:packagename
                local name_parts = name:split(":", {plain = true})
                if #name_parts == 2 then
                    data.package.namespace = name_parts[1]
                end
            end

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
    index_store._namespace = "local"
    if index_store:build_xpkg_index(xpkg_file) then
        index_store:save_to_local(true)
    end
    index_store._namespace = nil
end

function IndexManager:add_subrepo(indexrepo)
    -- avoid the case of "namespace:http://repo.git" - ':' issues
    if indexrepo:find("http", 1, true) then
        indexrepo = indexrepo:replace(":http", "::http"):split("::", {plain = true})
    else
        indexrepo = indexrepo:replace(":git", "::git"):split("::", {plain = true})
    end

    local namespace = indexrepo[1]
    local repo = indexrepo[2]

    if repo == nil then
        cprint("[xlings:xim]: ${red}invalid indexrepo format: ${clear}%s", indexrepo)
        cprint("\n\t${yellow}xim --add-indexrepo namespace:repo.git\n")
        return
    end

    repo_manager:add_subrepo(namespace, repo)
end

function IndexManager:mutex_package(pkgname)

    cprint("[xlings:xim]: checking [%s] for mutex groups...", pkgname)

    local mutex_pkgs = {}
    if self.index[pkgname] and self.__mutex_group then
        local _, pkg = utils.deref(self.index, pkgname)
        if pkg and pkg.mutex_group then
            for _, mgroup_name in pairs(pkg.mutex_group) do
                local mgroup = self.__mutex_group[mgroup_name]
                for _, pkgname in pairs(mgroup) do
                    pkgname = self:match_package_version(pkgname)
                    if self.index[pkgname] and self.index[pkgname].installed then
                        cprint("[xlings:xim]: ${dim yellow}mutex package found - ${green}%s", pkgname)
                        table.insert(mutex_pkgs, pkgname)
                    end
                end
            end
        end
    end
    return mutex_pkgs
end

function main()
    local index_manager = new()
    index_manager.status_changed_pkg["vscode"] = {installed = false}
    index_manager:update()
end