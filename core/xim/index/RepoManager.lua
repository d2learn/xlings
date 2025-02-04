-- remote package repo manager
-- 1. auto update - enable or disable | day week month
-- 2. multi-repo support
-- 3. private repo support

import("core.base.json")

import("config.xconfig")

import("xim.base.runtime")

local data_dir = runtime.get_xim_data_dir()

local RepoManager = {}
RepoManager.__index = RepoManager

function new()
    local config = xconfig.load()
    RepoManager.repo = config.xim["index-repo"]
    RepoManager.updateSchedule = 30   -- days
    return RepoManager
end

function RepoManager:sync()

    cprint("[xlings: xim]: sync main-indexrepo...")
    _sync_repo(self.repo)

    local main_repo_dir = _to_repodir(self.repo)
    local subrepos_file = path.join(main_repo_dir, "xim-subrepos.json")

    if os.isfile(subrepos_file) then
        cprint("[xlings: xim]: sync sub-indexrepos...")
        local subrepos = json.loadfile(subrepos_file)
        for _, repo in pairs(subrepos) do
            _sync_repo(repo, path.join(data_dir, "xim-index-subrepos"))
        end
    else
        cprint("[xlings: xim]: ${yellow}skip sync subrepos, file not found: %s", subrepos_file)
    end
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

    local subrepos_file = path.join(dirs["main"], "xim-subrepos.json")

    if os.isfile(subrepos_file) then
        local subrepos = json.loadfile(subrepos_file)
        for namespace, repo in pairs(subrepos) do
            dirs["subrepos"][namespace] = _to_repodir(repo, path.join(data_dir, "xim-index-subrepos"))
        end
    else
        cprint("[xlings: xim]: ${yellow}skip sync subrepos, file not found: %s", subrepos_file)
    end
    return dirs
end

function RepoManager:add_subrepo(namespace, repo)

    cprint("[xlings: xim]: add sub-indexrepo: ${yellow}%s${clear} => ${yellow}%s", namespace, repo)

    local main_repodir = _to_repodir(self.repo)
    local subrepos_file = path.join(main_repodir, "xim-subrepos.json")

    local subrepos = { }

    if os.isfile(subrepos_file) then
        subrepos = json.loadfile(subrepos_file)
    end

    subrepos[namespace] = repo

    json.savefile(subrepos_file, subrepos)
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
        os.cd(repodir)
        os.exec("git pull")
    else
        os.cd(rootdir)
        os.exec("git clone " .. repo)
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