import("xim.base.runtime")
import("xim.base.utils")
import("xim.pm.XPackage")
import("xim.pm.PkgManagerService")
import("xim.index.IndexManager")

local index_manager = IndexManager.new()
local pm_service = PkgManagerService.new()

--- class definition

local CmdProcessor = {}
CmdProcessor.__index = CmdProcessor

function new(target, cmds) --- create new object
    local instance = {}
    debug.setmetatable(instance, CmdProcessor)

    -- member variable
    instance.target = target -- target backup
    instance.cmds = cmds
    return instance
end

function CmdProcessor:run()
    if self.target and self.target ~= "" then
        if self.cmds.search then
            cprint("[xlings:xim]: search for *${green}%s${clear}* ...\n", self.target)
            self:search()
        else
            local pkg = index_manager:load_package(self.target)
            if pkg then
                self._pm_executor = pm_service:create_pm_executor(pkg)
                if self.cmds.remove then
                    self:remove()
                elseif self.cmds.update then
                    self:update()
                else
                    self:install()
                    _feedback()
                end
            else
                cprint("[xlings:xim]: ${red}package not found - %s${clear}", self.target)
                cprint("\n\t${yellow}Did you mean one of these?\n")
                self:search()
                cprint("[xlings:xim]: ${yellow}please check the name and try again${clear}")
            end
        end
    else
        if self.cmds.list then
            self:list()
        elseif self.cmds.detect then
            cprint("[xlings:xim]: start detect local packages...")
            self:detect()
        else
            self:help()
        end
    end
    index_manager:update()
end

function CmdProcessor:install(disable_info)
    if self._pm_executor:support() then
        local is_installed = self._pm_executor:installed(xpkg)

        if not disable_info then self._pm_executor:info(xpkg) end

        if is_installed then
            cprint("[xlings:xim]: already installed - ${green bright}%s${clear}", self.target)
            index_manager.status_changed_pkg[self.target] = {installed = true}
        else
            -- please input y or n
            if self.cmds.yes ~= true then
                local msg = string.format("${bright}install %s? (y/n)", self.target)
                if not utils.prompt(msg, "y") then
                    return
                end
            end

            local deps_list = self._pm_executor:deps(xpkg)
            if deps_list and not table.empty(deps_list) then
                cprint("[xlings:xim]: check ${bright green}" .. self.target .. "${clear} dependencies...")
                for _, dep_name in ipairs(deps_list) do
                    cprint("${dim}---${clear}")
                    new(dep_name, {yes = true}):install(true)
                    cprint("${dim}---${clear}")
                end
            end

            if self._pm_executor:install(xpkg) then
                cprint("[xlings:xim]: ${green bright}%s${clear} - installed", self.target)
                index_manager.status_changed_pkg[self.target] = {installed = true}
            else
                _feedback()
                local pkginfo = runtime.get_pkginfo()
                os.tryrm(pkginfo.install_file)
                cprint("[xlings:xim]: ${red}" .. self.target .. " install failed or not support, clear cache and retry${clear}")
                self:install(true)
            end
        end
    else
        cprint("[xlings:xim]: ${red}<%s>-platform not support${clear} - ${green}%s${clear}", os.host(), self.target)
    end
end

function CmdProcessor:info()
    self._pm_executor:info()
end

function CmdProcessor:list()
    local name_list = index_manager:search()
    for _, name in ipairs(name_list) do
        local pkg = index_manager:load_package(name)
        if pkg.installed then
            cprint("${dim bright}->${clear} ${green}%s", name)
        end
    end
end

function CmdProcessor:search()
    local name_list = index_manager:search(self.target)
    print(name_list)
end

function CmdProcessor:help()
    cprint("")
    cprint("\t${bright}XIM - Xlings Installation Manager${clear}")
    cprint("")

    cprint("${bright}xim version:${clear} pre-v0.0.1")
    cprint("")
    cprint("${bright}Usage1: $ ${cyan}xlings install [command] [target]")
    cprint("${bright}Usage2: $ ${cyan}xim [command] [target]")
    cprint("")

    cprint("${bright}Commands:${clear}")
    cprint("  ${magenta}-i${clear},   install,   install software/package/env")
    cprint("  ${magenta}-r${clear},   remove,    remove the software/package/env")
    cprint("  ${magenta}-u${clear},   update,    update the software/package/env")
    cprint("  ${magenta}-l${clear},   list,      list all installed software/packages/env")
    cprint("  ${magenta}-s${clear},   search,    search for a software/package")
    cprint("  ${magenta}-h${clear},   help,      display this help message")
    cprint("")

    cprint("${bright}SysCommands:${clear}")
    cprint("  ${magenta}--yes${clear},       yes to all prompts")
    cprint("  ${magenta}--detect${clear},    detect local software/packages")
    cprint("")

    cprint("${bright}Examples:${clear}")
    cprint("  ${cyan}xim vscode${clear}     -- install vscode")
    cprint("  ${cyan}xim -r vscode${clear}  -- remove/uninstall vscode")
    cprint("  ${cyan}xim -l${clear}         -- list all installed software/packages")
    cprint("  ${cyan}xim -s code${clear}    -- search for software/package with 'code'")

    cprint("")
    cprint("更多(More): ${underline}https://d2learn.org/xlings${clear}")
    cprint("")
end

function CmdProcessor:remove()
    if self._pm_executor:installed() then

        self._pm_executor:info()

        local confirm = self.cmds.yes
        if not confirm then
            local msg = string.format("${bright}uninstall/remove %s? (y/n)", self.target)
            confirm = utils.prompt(msg, "y")
        end
        if confirm then
            self._pm_executor:uninstall()
            index_manager.status_changed_pkg[self.target] = {installed = false}
            _feedback()
            cprint("[xlings:xim]: ${green bright}%s${clear} - removed", self.target)
        end
    else
        cprint("[xlings:xim]: ${yellow}package not installed${clear} - ${green}%s${clear}", self.target)
    end
end

function CmdProcessor:update()
    cprint("[xlings:xim]: update not implement")
end

function CmdProcessor:detect()
    local name_list = index_manager:search()
    for _, name in ipairs(name_list) do
        local pkg = index_manager:load_package(name)
        local pme = pm_service:create_pm_executor(pkg)
        if pme:installed() then
            cprint("${dim bright}->${clear} ${green}%s", name)
            index_manager.status_changed_pkg[name] = {installed = true}
        else
            index_manager.status_changed_pkg[name] = {installed = false}
        end
    end
end

--- module function

function _target_parse(target)
    -- exmaple xim@0.0.1@d2learn
    target = target or ""
    local targets = string.split(target, "@")
    return {
        name = targets[1],
        version = targets[2],
        maintainer = targets[3]
    }
end

function _feedback()
    cprint("\n\t     ${blue}反馈 & 交流 | Feedback & Discourse${clear}")
    cprint("\t${dim}(if encounter any problem, please report it)")
    cprint(
        "${bright}\n" ..
        "\thttps://forum.d2learn.org/category/9/xlings\n" ..
        "\thttps://github.com/d2learn/xlings/issues\n" ..
        "${clear}"
    )
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