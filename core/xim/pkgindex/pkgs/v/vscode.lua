package = {
    homepage = "https://code.visualstudio.com",

    name = "vscode",
    description = "Visual Studio Code",
    contributors = "https://github.com/microsoft/vscode/graphs/contributors",
    license = "MIT",
    repo = "https://github.com/microsoft/vscode",
    docs = "https://code.visualstudio.com/docs",

    status = "stable",
    categories = { "editor", "tools" },
    keywords = { "vscode", "cross-platform" },
    date = "2024-9-01",

    xpm = {
        ubuntu = {
            ["latest"] = { ref = "1.93.1" },
            ["1.93.1"] = {
                url =
                "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/code_1.93.1-1726079302_amd64.deb",
                sha256 = nil
            }
        },
    },

    pm_wrapper = {
        winget = "vscode",
        pacman = "code",
    }
}

import("common")
import("xim.base.runtime")

function installed()
    if os.host() == "windows" and (os.getenv("USERNAME") or ""):lower() == "administrator" then
        return os.isfile("C:\\Program Files\\Microsoft VS Code\\Code.exe")
    else
        -- os.exec("code --version") - windows cmd not support?
        common.xlings_exec("code --version") -- for windows
    end
    return true
end

function install()
    local pkginfo = runtime.get_pkginfo()
    local use_winget_sys = (os.getenv("USERNAME") or ""):lower() == "administrator"
    if os.host() == "windows" then
        print("[xlings]: runninng vscode installer, it may take some minutes...")
        if use_winget_sys then
            os.exec("winget install vscode --scope machine")
        else
            common.xlings_exec(pkginfo.install_file .. " /verysilent /suppressmsgboxes /mergetasks=!runcode")
        end
    elseif os.host() == "linux" then
        os.exec("sudo dpkg -i " .. pkginfo.install_file)
    elseif os.host() == "macosx" then
        -- TODO: install vscode on macosx
    end

    -- tips
    print([[
[xlings]: vscode Tips/小提示:
- auto save settings: File -> Auto Save
- 自动保存设置: 文件 -> 自动保存
    ]])

    return true
end

function uninstall()
    os.exec("sudo dpkg --remove code")
end
