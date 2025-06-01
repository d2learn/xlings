-- remote package repo manager
-- 1. auto update - enable or disable | day week month
-- 2. multi-repo support
-- 3. private repo support

import("devel.git")
import("core.base.json")

import("config.xconfig")

import("xim.base.runtime")
import("xim.base.utils")

local data_dir = runtime.get_xim_data_dir()
local index_reposdir = runtime.get_xim_index_reposdir()

local RepoManager = {}
RepoManager.__index = RepoManager

function new()
    local config = xconfig.load()
    RepoManager.repo = config.xim["index-repo"]
    RepoManager.updateSchedule = 30   -- days
    return RepoManager
end

function RepoManager:sync()

    cprint("[xlings: xim]: sync indexrepos...")

    -- 0. sync main repo
    _sync_repo(self.repo)

    local xim_indexrepos = { }

    -- 1. get main repo indexrepos
    local main_repo_dir = _to_repodir(self.repo)
    local main_indexrepos_file = path.join(main_repo_dir, "xim-indexrepos.lua")
    if os.isfile(main_indexrepos_file) then
        local main_indexrepos = utils.load_module(
            main_indexrepos_file, main_repo_dir
        ).xim_indexrepos
        for namespace, repo in pairs(main_indexrepos) do
            if type(repo) == "string" then
                xim_indexrepos[namespace] = repo
            else
                local config = xconfig.load()
                xim_indexrepos[namespace] = repo[config["mirror"]]
            end
        end
    end

    -- 2. get local indexrepos
    local local_indexrepos_file = path.join(index_reposdir, "xim-indexrepos.json")
    if os.isfile(local_indexrepos_file) then
        local local_indexrepos = json.loadfile(local_indexrepos_file)
        for namespace, repo in pairs(local_indexrepos) do
            xim_indexrepos[namespace] = repo
        end
    end

    cprint("[xlings: xim]: sync sub-indexrepos...")
    local xim_indexrepos_pass = { }
    for name, repo in pairs(xim_indexrepos) do
        try {
            function()
                _sync_repo(repo, index_reposdir)
                -- add to pass list
                xim_indexrepos_pass[name] = repo
            end,
            catch {
                function(errors)
                    cprint("[xlings: xim]: ${red}sync sub-indexrepo failed: %s", repo)
                    cprint(errors)
                end
            }
        }
    end

    -- save pass list
    cprint("[xlings: xim]: save sub-indexrepos pass list to %s", local_indexrepos_file)
    json.savefile(local_indexrepos_file, xim_indexrepos_pass)
end

function RepoManager:repodirs()
    local dirs = { }

    -- main repo
    local main_repodir = _to_repodir(self.repo)
    if not os.isdir(main_repodir) then
        self:sync()
    end

    dirs["main"] = main_repodir

    -- add subrepos
    dirs["subrepos"] = { }

    local subrepos_file = path.join(index_reposdir, "xim-indexrepos.json")

    if os.isfile(subrepos_file) then
        local subrepos = json.loadfile(subrepos_file)
        for namespace, repo in pairs(subrepos) do
            dirs["subrepos"][namespace] = _to_repodir(repo, index_reposdir)
        end
    else
        cprint("[xlings: xim]: ${yellow}skip sync sub-indexrepos, file not found: %s", subrepos_file)
    end
    return dirs
end

function RepoManager:add_subrepo(namespace, repo)

    cprint("[xlings: xim]: add sub-indexrepo: ${yellow}%s${clear} => ${yellow}%s", namespace, repo)

    try {
        function()
            _sync_repo(repo, index_reposdir)
        end,
        catch {
            function(errors)
                cprint("[xlings: xim]: ${red}add sub-indexrepo failed: %s", repo)
                raise(errors)
            end
        }
    }

    local subrepos_file = path.join(index_reposdir, "xim-indexrepos.json")

    local subrepos = { }

    if os.isfile(subrepos_file) then
        subrepos = json.loadfile(subrepos_file)
    end

    subrepos[namespace] = repo

    json.savefile(subrepos_file, subrepos)

    return _to_repodir(repo, index_reposdir)
end

function _sync_repo(repo, rootdir)
    print("[xlings: xim]: sync package index repo:", repo)

    rootdir = rootdir or data_dir

    if not os.isdir(rootdir) then
        cprint("[xlings: xim]: _sync_repo: create rootdir: %s", rootdir)
        os.mkdir(rootdir)
    end

    local repodir = _to_repodir(repo, rootdir)

    if os.isdir(repodir) then
        git.pull({repodir = repodir})
    else
        os.cd(rootdir)
        git.clone(repo)
    end

end

function _to_repodir(repo, rootdir)
    rootdir = rootdir or data_dir
    local dir = string.split(path.filename(repo), ".git")[1]
    local repodir = path.join(rootdir, dir)
    return repodir
end

function main()
    local repoManager = new()
    --repoManager:sync()
    print(repoManager:repodirs())
    --print(_to_repodir("my/myrepo"))
end