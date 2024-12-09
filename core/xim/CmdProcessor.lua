
import("XPackage")
import("index.IndexManager")

local index_manager = IndexManager.new()

--- class definition

local CmdProcessor = {}
CmdProcessor.__index = CmdProcessor

function new(target, args) --- create new object
    local instance = {}
    debug.setmetatable(instance, CmdProcessor)

    -- member variable
    instance._target = target -- target backup
    instance.target = _target_parse(target)
    instance.cmds = _cmds_parse(args)
    
    return instance
end

function CmdProcessor:run()
    if self._target and self._target ~= "" then
        if self.cmds.search then
            self:search()
        else
            local pkg = index_manager:load_package(self._target)
            if pkg.data == nil then
                cprint("[xlings:xim]: package not found - ${yellow}%s${clear}\n", self._target)
                self:search()
                cprint("[xlings:xim]: ${yellow}please input correct package name${clear}")
            elseif self.cmds.info then
                self:info()
            elseif self.cmds.remove then
                self:remove()
            elseif self.cmds.update then
                self:update()
            else
                self.cmds.info = true
                self:install()
            end
        end
    else
        if self.cmds.list then
            print("\tinstaller list")
            self:list()
        else
            self:help()
        end
    end
    index_manager:update()
end

function CmdProcessor:install()
    local pkg = index_manager:load_package(self._target)
    local xpkg = XPackage.new(pkg.data)
    if xpkg:support() then

        local is_installed = xpkg:installed()

        xpkg:info()

        if is_installed then
            cprint("[xlings:xim]: already installed - ${green bright}%s${clear}", self.target.name)
            index_manager.status_changed_pkg[self._target] = {installed = true}
        else
            -- please input y or n
            if self.cmds.yes ~= true then
                cprint("[xlings:xim]: ${bright}install %s? (y/n)", self.target.name)
                local confirm = io.read()
                if confirm ~= "y" then
                    return
                end
            end

            local deps_list = xpkg:deps()
            for _, dep in ipairs(deps_list) do
                new(dep, {"-y"}):install()
            end

            if xpkg:install() then
                cprint("[xlings:xim]: ${green bright}%s${clear} - installed", self.target.name)
                index_manager.status_changed_pkg[self._target] = {installed = true}
            else
                cprint("[xlings]: ${red}" .. self.target.name .. " install failed or not support, clear cache and retry${clear}")
                self:install()
            end
        end
    else
        cprint("[xlings:xim]: ${red}<%s>-platform not support${clear} - ${green}%s${clear}", os.host(), self.target.name)
    end
end

function CmdProcessor:info()
    local pkg = index_manager:load_package(self._target)
    local xpkg = XPackage.new(pkg.data)
    xpkg:info()
end

function CmdProcessor:list()
    local name_list = index_manager:search()
    for _, name in ipairs(name_list) do
        local pkg = index_manager:load_package(name)
        local xpkg = XPackage.new(pkg.data)
        if xpkg:installed() then
            index_manager.status_changed_pkg[name] = {installed = true}
            cprint("\n${dim}^%s\n", name)
        else
            index_manager.status_changed_pkg[name] = {installed = false}
        end
    end
end

function CmdProcessor:search()
    local name_list = index_manager:search(self._target)
    name_list = name_list or index_manager:search(self.target.name)
    print(name_list)
end

function CmdProcessor:help()
    cprint("${bright}xim version:${clear} pre-v0.0.1")
    cprint("")
    cprint("${bright}Usage1: $ ${cyan}xlings install [target] [options]")
    cprint("${bright}Usage2: $ ${cyan}xim [target] [options]")
    cprint("")

    cprint("${bright}Options:${clear}")
    cprint("  ${magenta}-i, -info${clear}     Display information about the software/package")
    cprint("  ${magenta}-r, -remove${clear}   Remove the software/package")
    cprint("  ${magenta}-u, -update${clear}   Update the software/package")
    cprint("  ${magenta}-l, -list${clear}     List all installed software/packages")
    cprint("  ${magenta}-s, -search${clear}   Search for a software/package")
    cprint("  ${magenta}-h, -help${clear}     Display this help message")

    cprint("")
    cprint("更多(More): ${underline}https://d2learn.org/xlings${clear}")
    cprint("")
end

function CmdProcessor:remove()
    print("remove not implement")
end

function CmdProcessor:update()
    print("update not implement")
end

--- module function

function _target_parse(target)
    -- exmaple xim@0.0.1@d2learn
    target = target or ""
    local targets = string.split(target, "@")
    return {
        name = targets[1],
        maintainer = targets[2],
        version = targets[3]
    }
end

function _cmds_parse(args)
    args = args or {}
    local cmds = {
        ["-i"] = false,  -- -info
        ["-info"] = false,
        ["-r"] = false, -- -remove/uninstall
        ["-remove"] = false,
        ["-u"] = false, -- -update
        ["-update"] = false,
        ["-l"] = false, -- -list
        ["-list"] = false,
        ["-s"] = false, -- -search
        ["-search"] = false,
        ["-h"] = false, -- -help
        ["-help"] = false,
        ["-y"] = false, -- -yes
        ["-yes"] = false,
        ["-v"] = false, -- -version
        ["-version"] = false
    }
    for i = 1, #args do
        if cmds[args[i]] ~= nil then
            cmds[args[i]] = true
        end
    end

    return {
        info = cmds["-i"] or cmds["-info"],
        remove = cmds["-r"] or cmds["-remove"],
        update = cmds["-u"] or cmds["-update"],
        list = cmds["-l"] or cmds["-list"],
        search = cmds["-s"] or cmds["-search"],
        help = cmds["-h"] or cmds["-help"],
        yes = cmds["-y"] or cmds["-yes"],
        version = cmds["-v"] or cmds["-version"]
    }
end

function _test()
    local cmds = _cmds_parse({"-i", "-remove"})
    print(cmds)

    local target = _target_parse("xlings-d2learn-0.0.1")
    print(target)
end

function main(info)
    _test()
end