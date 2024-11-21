import("installer.base.utils")

import("installer.vscode")
import("installer.mdbook")
import("installer.gcc")
import("installer.c_and_cpp")
import("installer.python")
import("installer.rust")
import("installer.nvm")
import("installer.nodejs")
import("installer.npm")
import("installer.pnpm")
import("installer.project_graph")
import("installer.fnm")
import("installer.openjdk8")
import("installer.java")
import("installer.dotnet")
import("installer.csharp")

local supported_installers = {
    ["vscode"]    = vscode,
    ["mdbook"]    = mdbook,
    ["c"]         = c_and_cpp,
    ["cpp"]       = c_and_cpp,
    ["c++"]       = c_and_cpp,
    ["gcc"]       = gcc,
    ["g++"]       = gcc,
    ["python"]    = python,
    ["python3"]   = python,
    ["rust"]      = rust,
    ["nvm"]       = nvm,
    ["nodejs"]    = nodejs,
    ["npm"]       = npm,
    ["pnpm"]      = pnpm,
    ["project-graph"] = project_graph,
    ["fnm"] = fnm,
    ["openjdk8"] = openjdk8,
    ["java"] = java,
    ["dotnet"] = dotnet,
    ["csharp"] = csharp,
}

for k, v in pairs(utils.load_installers("windows")) do
    supported_installers[k] = v
end

-- alias
supported_installers["devc++"]    = supported_installers.devcpp
supported_installers["vc++6.0"]   = supported_installers.vcpp6
supported_installers["vs"]        = supported_installers.visual_studio
supported_installers["c#"]        = supported_installers.csharp

function list()
    cprint("\t${bright}xinstaller lists${clear}")
    for name, x_installer in pairs(supported_installers) do
        if x_installer.support()[os.host()] then
            print(" " .. name)
        end
    end
end

function get_installer(name)
    local installer = supported_installers[name]
    if installer then
        return installer
    else
        cprint("[xlings]: ${red}installer not found${clear} - ${green}%s${clear}", name)
    end
end


local function print_info(info, name)
    if not info then return end

    cprint("\n--- ${cyan}info${clear}")

    local fields = {
        {key = "name", label = "name"},
        {key = "homepage", label = "homepage"},
        {key = "author", label = "author"},
        {key = "maintainers", label = "maintainers"},
        {key = "licenses", label = "licenses"},
        {key = "github", label = "github"},
        {key = "docs", label = "docs"},
        --{key = "profile", label = "profile"}
    }

    cprint("")
    for _, field in ipairs(fields) do
        local value = info[field.key]
        if value then
            cprint(string.format("${bright}%s:${clear} ${dim}%s${clear}", field.label, value))
        end
    end

    cprint("")

    if info["profile"] then
        cprint( "\t${green bright}" .. info["profile"] .. "${clear}")
    end

    cprint("")

end

function support(name)
    local x_installer = get_installer(name)
    if x_installer then
        if x_installer.support()[os.host()] then
            return true
        else
            cprint("[xlings]: ${red}<%s>-platform not support${clear} - ${green}%s${clear}", os.host(), name)
        end
    end

    return false

end

function installed(name)
    local x_installer = get_installer(name)
    if x_installer then
        return x_installer.installed()
    end
    return false
end

function install(name, x_installer, req_confirm)

    -- please input y or n
    if req_confirm then
        cprint("[xlings]: ${bright}install %s? (y/n)", name)
        local confirm = io.read()
        if confirm ~= "y" then
            return
        end
    end

    if x_installer.deps then
        local deps_list = x_installer.deps()[os.host()]
        if deps_list and not table.empty(deps_list) then
            cprint("[xlings]: check ${bright green}" .. name .. "${clear} dependencies...")
            for _, dep_name in ipairs(deps_list) do
                cprint("${dim}---${clear}")
                main(dep_name, {confirm = false, info = false, feedback = false})
            end
            cprint("${dim}---${clear}")
        end
    end

    local success = x_installer.install()

    if success then
        cprint("[xlings]: ${green bright}" .. name .. "${clear} already installed(${yellow}takes effect in a new terminal or cmd${clear})")
    else
        cprint("[xlings]: ${red}" .. name .. " install failed or not support, clear cache and retry${clear}")
        install(name, x_installer, true)
    end

end

function uninstall()
    -- TODO
end

function mpkger_installer(name)
    -- TODO
end

function main(name, options)

    if options == nil then
        options = { -- default config
            confirm = true,
            info = false,
            feedback = false,
        }
    end

    if support(name) then
        local x_installer = get_installer(name)
        local is_installed = x_installer.installed()

        if x_installer.info and options.info then
            local info = x_installer.info()
            print_info(info, name)
        end

        if is_installed then
            cprint("[xlings]: already installed - ${green bright}" .. name .. "${clear}")
        else
            install(name, x_installer, options.confirm)
        end

    end

    if options.feedback then

        cprint("\n\t\t${blue}反馈 & 交流 | Feedback & Discourse${clear}")
        cprint(
            "${bright}\n" ..
            "\thttps://forum.d2learn.org/category/9/xlings\n" ..
            "\thttps://github.com/d2learn/xlings/issues\n" ..
            "${clear}"
        )

    end

end