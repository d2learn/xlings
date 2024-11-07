import("installer.vscode")
import("installer.visual_studio")
import("installer.mdbook")
import("installer.c_and_cpp")
import("installer.python")
import("installer.devcpp")

local supported_installers = {
    ["vscode"]    = vscode,
    ["mdbook"]    = mdbook,
    ["vs"]        = visual_studio,
    ["devcpp"]   = devcpp,
    ["devc++"]   = devcpp,
    ["c"]         = c_and_cpp,
    ["gcc"]       = c_and_cpp,
    ["cpp"]       = c_and_cpp,
    ["c++"]       = c_and_cpp,
    ["g++"]       = c_and_cpp,
    ["python"]    = python,
    ["python3"]   = python,
}

function list()
    cprint("\t${bright}xinstaller lists${clear}")
    for name, _ in pairs(supported_installers) do
        print(" " .. name)
    end
end

function get_installer(name)
    local installer = supported_installers[name]
    if installer then
        return installer
    else
        cprint("[xlings]: ${red}installer not found${clear} - ${green}%s${clear}", name)
        cprint(
            "[xlings]: ${yellow}feedback to${clear} " ..
            "${bright}" ..
            "https://github.com/d2learn/xlings/issues/new" ..
            "${clear}"
        )
    end
end

function installed(name) 
    local x_installer = get_installer(name)
    if x_installer then
        return x_installer.installed()
    end
    return false
end

function support(name)
    return get_installer(name) ~= nil
end

function install(name, x_installer)
    if x_installer.installed() then
        return
    end

    -- please input y or n
    cprint("[xlings]: ${bright}install %s? (y/n)", name)
    local confirm = io.read()
    if confirm ~= "y" then
        return
    end

    local success = x_installer.install()

    cprint("")
    cprint("[xlings]: ${yellow bright}反馈(feedback)${clear}: ${underline}https://forum.d2learn.org/category/9/xlings${clear}")
    cprint("")

    if success then
        cprint("[xlings]: ${green bright}" .. name .. "${clear} already installed(${yellow}takes effect in a new terminal or cmd${clear})")
    else
        cprint("[xlings]: ${red}" .. name .. " install failed or not support, clear cache and retry${clear}")
        install(name, x_installer)
    end

end

function main(name)
    local x_installer = get_installer(name)
    if x_installer then
        cprint("${dim}-%s${clear}", name)
        install(name, x_installer)
    end
end