import("installer.vscode")
import("installer.visual_studio")
import("installer.mdbook")
import("installer.gcc")
import("installer.c_and_cpp")
import("installer.python")
import("installer.devcpp")
import("installer.vcpp6")

local supported_installers = {
    ["vscode"]    = vscode,
    ["mdbook"]    = mdbook,
    ["vs"]        = visual_studio,
    ["devcpp"]    = devcpp,
    ["devc++"]    = devcpp,
    ["vc++6.0"]    = vcpp6,
    ["c"]         = c_and_cpp,
    ["cpp"]       = c_and_cpp,
    ["c++"]       = c_and_cpp,
    ["gcc"]       = gcc,
    ["g++"]       = gcc,
    ["python"]    = python,
    ["python3"]   = python,
}

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

function install(name, x_installer)

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
    if support(name) then
        cprint("${dim}-%s${clear}", name)
        local x_installer = get_installer(name)
        if x_installer.installed() then
            cprint("[xlings]: already installed - ${green bright}" .. name .. "${clear}")
        else
            install(name, x_installer)
        end
    else
        cprint(
            "[xlings]: ${yellow}反馈 | Feedback to${clear}\n" ..
            "${bright}\n" ..
            "\thttps://forum.d2learn.org/category/9/xlings\n" ..
            "\thttps://github.com/d2learn/xlings/issues\n" ..
            "${clear}"
        )
    end
end