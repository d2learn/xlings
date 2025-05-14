-- xim user interface and core-program entry point

import("lib.detect.find_tool")

import("common")

import("xim.base.utils")
import("xim.base.runtime")
import("xim.pm.XPackage")
import("xim.pm.PkgManagerService")
import("xim.index.IndexManager")

local index_manager = IndexManager.new()
local pm_service = PkgManagerService.new()
local runtime_stack = { }

--- class definition

local CmdProcessor = {}
CmdProcessor.__index = CmdProcessor

function new(target, cmds) --- create new object
    local instance = {}
    debug.setmetatable(instance, CmdProcessor)

    -- member variable
    instance.target = target -- target backup
    instance.cmds = cmds

    index_manager:init()

    return instance
end

function CmdProcessor:run()
    -- push runtime stack
    table.insert(runtime_stack, utils.deep_copy(runtime.get_runtime_data()))

        runtime.init()
        runtime.set_runtime_data({input_args = self.cmds})

        if self.target and self.target ~= "" then
            self:run_target_cmds()
        else
            self:run_nontarget_cmds()
        end
        index_manager:update()

    -- Note: pop sequence is important
    -- pop runtime stack
    runtime_data = table.remove(runtime_stack)

        runtime.set_runtime_data(runtime_data)
        self.cmds = runtime_data.input_args
end

function CmdProcessor:run_target_cmds()
-- target isnt nil - [is a package name]

    if self.cmds.search then
        cprint("[xlings:xim]: search for *${green}%s${clear}* ...\n", self.target)
        self:search()
    else

        -- support namespace::pkgname
        -- support namespace:pkgname
        self.target = self.target:replace("::", ":")

        local target_pkgname = index_manager:match_package_version(self.target)
        if target_pkgname then
            if not self.cmds.info_json then
                cprint("${dim}[xlings:xim]: create pm executor for <%s> ... ${clear}", self.target)
            end

            local input_target = self.target
            local pkg = index_manager:load_package(target_pkgname)
            self._pm_executor = pm_service:create_pm_executor(pkg)
            self.target = target_pkgname

            if self.cmds.info_json then
                local pkginfo = self._pm_executor._pkg:info()
                local deps_list = self._pm_executor:deps()
                pkginfo["dependencies"] = {}
                for _, dep_name in ipairs(deps_list) do
                    pkginfo.dependencies[dep_name] = ""
                end
                import("core.base.json")
                print(json.encode(pkginfo))
            elseif self.cmds.remove then
                self:remove()
            elseif self.cmds.update then
                self:update()
            else -- install
                -- if dont care for version, try to find installed version
                local installed_version = nil
                if not input_target:find("@", 1, true) then
                    if self._pm_executor:installed() then
                        installed_version = target_pkgname
                    end
                    -- only support tips system version, but dont manage it
                    if not installed_version and find_tool(input_target) then
                        installed_version = input_target .. "@system"
                    end
                end

                if installed_version then
                    cprint("[xlings:xim]: ${green bright}%s${clear} - already installed", installed_version)
                else
                    self:install()
                    self:_feedback()
                end
            end
        else
            local target_list = ""
            local matched_target = index_manager:search(self.target)
            for name, _ in pairs(matched_target) do
                target_list = target_list .. name .. "\n"
            end
            -- fzf installed, use fzf to select target
            if target_list ~= "" and find_tool("fzf") then
                local tmpfile = path.join(runtime.get_xim_data_dir(), ".xim_fzf_out")
                if is_host("windows") then
                    io.writefile(tmpfile, target_list)
                    os.addenv("FZF_DEFAULT_COMMAND", string.format([[type %s]], tmpfile))
                else
                    io.writefile(tmpfile, "")
                    os.addenv("FZF_DEFAULT_COMMAND", string.format([[echo -e "%s"]], target_list))
                end
                try {
                    function()
                        if is_host("windows") then
                            common.xlings_exec([[fzf --preview "xim -i {}" > ]] .. tmpfile)
                        else
                            os.execv("fzf", {"--print-query", "--preview", "xim -i {}"}, {stdout = tmpfile})
                        end
                        self.target = io.readfile(tmpfile):trim()
                        self:run_target_cmds()
                    end, catch {
                        function() end
                    }
                }
            else
                cprint("[xlings:xim]: ${red}package not found - %s${clear}", self.target)
                self:target_tips()
            end
        end
    end
end

function CmdProcessor:run_nontarget_cmds()
-- target is nil
    if self.cmds.list then
        self:list()
    elseif self.cmds.sysdetect then
        self:sys_detect()
    elseif self.cmds.sysupdate then
        self:sys_update()
    elseif self.cmds.sysadd_xpkg then
        self:sys_add_xpkg()
    elseif self.cmds.sysadd_indexrepo then
        self:sys_add_indexrepo()
    else
        self:help()
    end
end

function CmdProcessor:install()
    if self._pm_executor:support() then
        local is_installed = self._pm_executor:installed()

        self:info()

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

            local deps_list = self._pm_executor:deps()
            if deps_list and not table.empty(deps_list) then
                cprint("[xlings:xim]: check ${bright green}" .. self.target .. "${clear} dependencies...")
                for _, dep_name in ipairs(deps_list) do
                    cprint("${dim}---${clear}" .. dep_name)
                    new(dep_name, {
                        install = true,
                        yes = true,
                        disable_info = true
                    }):run()
                    cprint("${dim}---${clear}")
                end
            end

            if self._pm_executor:install(xpkg) then
                self:_restart_tips()
                cprint("[xlings:xim]: ${green bright}%s${clear} - installed", self.target)
                index_manager.status_changed_pkg[self.target] = {installed = true}
            else
                self:_feedback()
                local pkginfo = runtime.get_pkginfo()
                os.tryrm(pkginfo.install_file)
                cprint("[xlings:xim]: ${red}" .. self.target .. " install failed or not support, clear cache and retry${clear}")
                self.cmds.yes = false -- retry need confirm
                self:install(true)
            end
        end
    else
        cprint(
            "[xlings:xim]: ${red}<%s>-platform not support${clear} - ${green}%s${clear}",
            utils.os_info().name,
            self.target
        )
    end
end

function CmdProcessor:info()
    if self.cmds.disable_info then return end
    self._pm_executor:info()
end

function CmdProcessor:list()
    local names_table = index_manager:search(self.cmds.list)
    for name, alias in pairs(names_table) do
        local pkg = index_manager:load_package(name)
        if pkg.installed then
            alias_str = table.concat(alias, ", ")
            cprint(
                "${dim bright}->${clear} ${green}%s${clear} ${dim}(%s)",
                name,
                alias_str
            )
        end
    end
end

function CmdProcessor:search(opt)
    opt = opt or {}
    local names = index_manager:search(self.target, opt)
    print(names)
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
            if self._pm_executor:uninstall() and not self._pm_executor:installed() then
                index_manager.status_changed_pkg[self.target] = {installed = false}
                cprint("[xlings:xim]: ${green bright}%s${clear} - removed", self.target)
            end
            self:_feedback()
        end
    else
        cprint("[xlings:xim]: ${yellow}package not installed${clear} - ${green}%s${clear}", self.target)
        index_manager.status_changed_pkg[self.target] = {installed = false}
        self:target_tips({ installed = true })
    end
end

function CmdProcessor:update()
    cprint("[xlings:xim]: update not implement")
end

function CmdProcessor:sys_detect()
    cprint("[xlings:xim]: start detect local packages...")
    local names_table = index_manager:search()
    for name, alias in pairs(names_table) do
        local pkg = index_manager:load_package(name)
        local pme = pm_service:create_pm_executor(pkg)
        if pme:installed() then
            alias_str = table.concat(alias, ", ")
            cprint("${dim bright}->${clear} ${green}%s${clear} ${dim}(%s)", name, alias_str)
            index_manager.status_changed_pkg[name] = {installed = true}
        else
            index_manager.status_changed_pkg[name] = {installed = false}
        end
    end
end

function CmdProcessor:sys_update()
    local datadir = runtime.get_xim_data_dir()
    if self.cmds.sysupdate == "index" then
        index_manager:sync_repo()
        index_manager:rebuild()
        self:sys_detect()
    elseif self.cmds.sysupdate == "self" then
        cprint("[xlings:xim]: update self - todo")
    end
end

function CmdProcessor:sys_add_xpkg()
    local xpkg_file = self.cmds.sysadd_xpkg
    if not os.isfile(xpkg_file) then
        if xpkg_file:find("http", 1, true) and xpkg_file:find(".lua", 1, true) then
            local index_repodir = runtime.get_xim_local_index_repodir()
            local local_xpkg_file = path.join(index_repodir, path.filename(xpkg_file))
            if os.isfile(local_xpkg_file) then
                cprint("[xlings:xim]: ${yellow}remove old xpkg-file: %s${clear}", local_xpkg_file)
                os.tryrm(local_xpkg_file)
            end
            --cprint("[xlings:xim]: download xpkg-file from:${dim} - %s", xpkg_file)
            _, local_xpkg_file = utils.try_download_and_check(xpkg_file, index_repodir)
            cprint("[xlings:xim]: %s - ${green}ok", local_xpkg_file)
            xpkg_file = local_xpkg_file
        else
            cprint("[xlings:xim]: ${dim}convert xpkg-file to runtime path${clear} - %s", xpkg_file)
            xpkg_file = path.join(runtime.get_rundir(), xpkg_file)
        end
    else
        xpkg_file = path.absolute(xpkg_file)
    end

    if os.isfile(xpkg_file) then
        index_manager:add_xpkg(xpkg_file)
    else
        cprint("[xlings:xim]: ${red}xpkg-file not found${clear} - %s", xpkg_file)
    end
end

function CmdProcessor:sys_add_indexrepo()
    local indexrepo = self.cmds.sysadd_indexrepo
    index_manager:add_subrepo(indexrepo)
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

function CmdProcessor:help()
    cprint("")
    cprint("\t${bright}XIM - Xlings Installation Manager${clear}")
    cprint("")

    cprint("${bright}xim version:${clear} pre-v0.0.2")
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
    cprint("  ${magenta}--detect${clear},        detect local software/packages")
    cprint("  ${magenta}--update${clear},        update [self | index]")
    cprint("  ${magenta}--add-xpkg${clear},      add xpkg file to index database")
    cprint("  ${magenta}--xpkg-args${clear},     xpkg args")
    cprint("  ${magenta}--add-indexrepo${clear}, add indexrepo to repo manager")
    cprint("  ${magenta}--disable-info${clear},  disable info display")
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

function CmdProcessor:target_tips(opt)
    cprint("\n\t${yellow}Did you mean one of these?\n")
    self:search(opt)
    cprint("[xlings:xim]: ${yellow}please check the name and try again${clear}")
end

function CmdProcessor:_restart_tips()
    if self.cmds.disable_info then return end
    cprint("\n\t ${yellow}**maybe need to restart cmd/shell to load env**${clear}")
    cprint("\t       ${dim}try to run${clear} source ~/.bashrc\n")
end

function CmdProcessor:_feedback()
    if self.cmds.disable_info then return end
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