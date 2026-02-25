-- xim user interface and core-program entry point

import("lib.detect.find_tool")

import("common")

import("base.log")
import("config.i18n")

import("base.utils")
import("base.runtime")
import("base.xvm")
import("platform")
import("pm.XPackage")
import("pm.PkgManagerService")
import("index.IndexManager")

local function _resolve_dep_dir_via_xvm(name, version)
    local old_value = xvm.log_tag(false)
    local pinfo = xvm.info(name, version or "")
    xvm.log_tag(old_value)
    if not pinfo or not pinfo["SPath"] or not pinfo["Version"] then
        return nil
    end

    local spath = pinfo["SPath"]
    local pver = pinfo["Version"]
    local strs = string.split(spath, pver)
    if #strs == 2 then
        return path.join(strs[1], pver)
    end
    return nil
end

local function _resolve_dep_dir_via_scan(name, version)
    local xpkgs = runtime.get_xim_install_basedir()
    local try_names = {name, "scode-x-" .. name, "fromsource-x-" .. name}
    for _, try_name in ipairs(try_names) do
        local dep_root = path.join(xpkgs, try_name)
        if os.isdir(dep_root) then
            local ver_dir = version
            if not ver_dir then
                local vers = os.dirs(dep_root .. "/*")
                if vers and #vers > 0 then
                    table.sort(vers)
                    ver_dir = path.basename(vers[#vers])
                end
            end
            if ver_dir then
                local install_dir = path.join(dep_root, ver_dir)
                if os.isdir(install_dir) then
                    return install_dir
                end
            end
        end
    end
    return nil
end

-- Aggregate dep libs (*.so) from xpkgs into target_libdir (e.g. subos/<name>/lib) so apps like d2x find glibc/openssl
local function aggregate_dep_libs_to(deps_list, target_libdir)
    if not deps_list or not is_host("linux") or not target_libdir then return end
    for _, dep_spec in ipairs(deps_list) do
        local name = dep_spec:gsub("@.*", "")
        local ver_opt = dep_spec:find("@", 1, true) and dep_spec:match("@(.+)") or nil
        local install_dir = _resolve_dep_dir_via_xvm(name, ver_opt)
        if not install_dir then
            install_dir = _resolve_dep_dir_via_scan(name, ver_opt)
        end
        if install_dir and os.isdir(install_dir) then
            for _, libsub in ipairs({"lib64", "lib"}) do
                local srcdir = path.join(install_dir, libsub)
                if os.isdir(srcdir) then
                    if not os.isdir(target_libdir) then os.mkdir(target_libdir) end
                    for _, f in ipairs(os.files(path.join(srcdir, "*.so*"))) do
                        os.ln(f, path.join(target_libdir, path.filename(f)), {force = true})
                    end
                    break
                end
            end
        end
    end
end

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
    runtime.set_runtime_data({rundir = os.curdir()})
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
        os.cd(runtime_data.rundir)
end

function CmdProcessor:run_target_cmds()
-- target isnt nil - [is a package name]
-- (self is handled by C++ via xmake xself task, not xim)

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
            self._input_target = input_target -- backup

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

                if self._pm_executor:installed() then
                    installed_version = target_pkgname
                end

                if not installed_version and (not input_target:find("@", 1, true)) then

                    -- TODO: better way to detect apps for install by other methods
                    -- for complex detect, it should implemented in xpkg.lua's installed
                    -- system tool detect helper function
                    function system_tool_detect(input_target)
                        local tool = find_tool(input_target)
                        -- adapt for windows, if not found .exe, try .bat or .cmd
                        if not tool and is_host("windows") then
                            tool = find_tool(input_target .. ".bat")
                            if not tool then
                                tool = find_tool(input_target .. ".cmd")
                            end
                        end

                        -- TODO: find_tool design issue? avoid musl-cross-make -> make
                        -- https://github.com/xmake-io/xmake/blame/7ab1594d2578a0b31d0d09ed2ac813a8be0ee680/xmake/modules/lib/detect/find_toolname.lua#L100
                        if tool and tool.name then
                            return tool.name:find(input_target, 1, true)
                        end

                        return tool -- to false/true?
                    end

                    local exist_system_version = system_tool_detect(input_target)

                    if not exist_system_version and self._pm_executor.type == "xpm" then
                        exist_system_version = system_tool_detect(self._pm_executor._pkg.name)
                    end

                    -- only support tips system version, but dont manage it
                    if not installed_version and exist_system_version then
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
            local matched_target_nums = 0
            local matched_target = index_manager:search(self.target)

            for name, _ in pairs(matched_target) do
                matched_target_nums = matched_target_nums + 1
                target_list = target_list .. name .. "\n"
            end

            if matched_target_nums == 1 then
                self.target = target_list:trim()
                self:run_target_cmds()
            else
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
            end -- if matched_target and #matched_target == 1 then
        end
    end
end

function CmdProcessor:run_nontarget_cmds()
-- target is nil
    if self.cmds.list then
        self:list()
    elseif self.cmds.update then
        self:update()
    elseif self.cmds.install_config_xlings and self.cmds.install_config_xlings ~= "" then
        self:install_from_legacy_config_xlings()
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

function CmdProcessor:install_from_legacy_config_xlings()
    local cfg = self.cmds.install_config_xlings
    if not cfg or cfg == "" then
        raise("missing --install-config-xlings argument")
    end
    if not os.isfile(cfg) then
        cfg = path.absolute(cfg)
    end
    if not os.isfile(cfg) then
        raise("config.xlings not found: " .. tostring(self.cmds.install_config_xlings))
    end

    -- Compatibility path: copy config.xlings into local .xlings module file,
    -- then load by utils.load_module so parsing/execution stays on xmake side.
    local cfgdir = path.directory(cfg)
    local compat_dir = path.join(cfgdir, ".xlings")
    if not os.isdir(compat_dir) then
        os.mkdir(compat_dir)
    end

    local cfg_content = io.readfile(cfg)
    if not cfg_content or cfg_content == "" then
        raise("read config.xlings failed: " .. cfg)
    end

    local compat_file = path.join(compat_dir, "__legacy_config_xlings.lua")
    -- Keep original content, append a return table so module loader can read fields.
    local wrapped = cfg_content .. "\n\nreturn { xim = xim, xdeps = xdeps }\n"
    io.writefile(compat_file, wrapped)

    local mod = nil
    try {
        function()
            mod = utils.load_module(compat_file, compat_dir)
        end,
        catch {
            function(e)
                raise("load legacy config module failed: " .. tostring(e))
            end
        }
    }
    local ximcfg = mod and (mod.xim or mod.xdeps)
    if type(ximcfg) ~= "table" then
        raise("missing deps table (`xim` or `xdeps`) in " .. cfg)
    end

    local targets = {}
    for name, ver in pairs(ximcfg) do
        if type(name) == "string" and name ~= "xppcmds" and type(ver) == "string" then
            local dep = name
            if ver ~= "" then
                dep = name .. "@" .. ver
            end
            table.insert(targets, dep)
        end
    end
    table.sort(targets)

    if #targets == 0 then
        raise("no installable deps in xim table: " .. cfg)
    end

    cprint("[xlings:xim]: install deps from legacy config - %s", cfg)
    for _, dep in ipairs(targets) do
        cprint("${dim}---${clear}" .. dep)
        new(dep, {
            install = true,
            yes = true,
            disable_info = true
        }):run()
        cprint("${dim}---${clear}")
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

            local mutex_pkgs = index_manager:mutex_package(self.target)
            if #mutex_pkgs > 0 then
                cprint("[xlings:xim]: ${yellow bright}mutex package found, need remove them first${clear}")
                for _, pkgname in ipairs(mutex_pkgs) do
                    cprint("${dim bright} - ${green bright}%s${clear}", pkgname)
                end
                local msg = string.format("${bright yellow}remove all? (y/n)", self.target)
                if not utils.prompt(msg, "y") then
                    return
                else
                    for _, pkgname in ipairs(mutex_pkgs) do
                        cprint("${dim}---${clear}" .. pkgname)
                        new(pkgname, {
                            remove = true,
                            yes = true,
                            disable_info = true
                        }):run()
                        cprint("${dim}---${clear}")
                    end
                end
            end

            local deps_list = self._pm_executor:deps()
            local resolved_deps_list = {}
            if deps_list and not table.empty(deps_list) then
                cprint("[xlings:xim]: check ${bright green}" .. self.target .. "${clear} dependencies...")
                for _, dep_name in ipairs(deps_list) do
                    local dep_pkgname = index_manager:match_package_version(dep_name)
                    if not dep_pkgname then
                        cprint("${dim}[xlings:xim]: skip dependency (not in index): %s${clear}", dep_name)
                    else
                        table.insert(resolved_deps_list, dep_pkgname)
                        cprint("${dim}---${clear}" .. dep_name)
                        new(dep_name, {
                            install = true,
                            yes = true,
                            disable_info = true
                        }):run()
                        cprint("${dim}---${clear}")
                    end
                end
            end

            if self._pm_executor:install({
                resolved_deps_list = resolved_deps_list
            }) then
                self:_restart_tips()
                local cfg = platform.get_config_info()
                if cfg.subosdir and deps_list and not table.empty(deps_list) then
                    local target_lib = path.join(cfg.subosdir, "lib")
                    aggregate_dep_libs_to(deps_list, target_lib)
                end
                cprint("[xlings:xim]: ${green bright}%s${clear} - installed", self.target)
                if self.cmds.sys_use then
                    self._pm_executor:use()
                end
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

        self:info()

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
    cprint("[xlings:xim]: ${dim bright}sync repo and rebuild index...${clear}")

    index_manager:sync_repo(true)
    index_manager:rebuild()
    self:sys_detect()

    -- TODO: support install latest version for target
    if self._input_target and self._input_target ~= "" then
        cprint("[xlings:xim]: ${bright}try to update [ %s ] to latest version...${clear}", self._input_target)
        new(self._input_target .. "@latest", {
            yes = true,
            disable_info = true,
            sys_use = true
        }):run()
    end
end

function CmdProcessor:sys_detect()
    cprint("[xlings:xim]: start detect local packages...")
    local names_table = index_manager:search()
    for name, alias in pairs(names_table) do
        local pkg = index_manager:load_package(name)
        if not pkg.pmwrapper then -- TODO: add cmd-arg to control?
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
end

function CmdProcessor:sys_update()
    local datadir = runtime.get_xim_data_dir()
    if self.cmds.sysupdate == "index" then
        index_manager:sync_repo(true)
        index_manager:rebuild()
        self:sys_detect()
    elseif self.cmds.sysupdate == "self" then
        import("xself").main("update")
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
    log.i18n_print(i18n.data()["xim"].help)
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
