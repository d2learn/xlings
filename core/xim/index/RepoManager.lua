-- remote package repo manager
-- 1. auto update - enable or disable | day week month
-- 2. multi-repo support
-- 3. private repo support

import("xim.base.runtime")

local data_dir = runtime.get_xim_data_dir()

local RepoManager = {}
RepoManager.__index = RepoManager

function new()
    RepoManager.repos = {
        "https://github.com/d2learn/xim-pkgindex.git",
    }
    RepoManager.updateSchedule = 30   -- days
    return RepoManager
end

function RepoManager:update()
    for _, repo in ipairs(self.repos) do
        print("[xlings: xim]: updating package index repo:", repo)
        local repodir = _to_repodir(repo)
        if os.isdir(repodir) then
            os.cd(repodir)
            os.exec("git pull")
        else
            os.cd(data_dir)
            os.exec("git clone " .. repo)
        end
    end
end

function RepoManager:repodirs()
    local dirs = {}
    for _, repo in ipairs(self.repos) do
        local repodir = _to_repodir(repo)
        if not os.isdir(repodir) then
            os.cd(data_dir)
            os.exec("git clone " .. repo)
        end
        table.insert(dirs, repodir)
    end
    return dirs
end

function _to_repodir(repo)
    local dir = string.split(path.filename(repo), ".git")[1]
    local repodir = path.join(data_dir, dir)
    return repodir
end

function main()
    local repoManager = new()
    repoManager:update()
    print(repoManager:repodirs())
    print(_to_repodir("my/myrepo"))
end