package = {
    homepage = "https://code.visualstudio.com",

    name = "vscode",
    version = "1.9.3",
    description = "Visual Studio Code",
    contributor = "https://github.com/microsoft/vscode/graphs/contributors",
    license = "MIT",
    repo = "https://github.com/microsoft/vscode",
    docs = "https://code.visualstudio.com/docs",

    status = "stable",
    categories = {"editor", "tools"},
    keywords = {"vscode", "cross-platform"},
    date = "2024-9-01",

    support = {
        windows = true,
        ubuntu = true,
        arch = false, -- TODO: add arch support
    },

    pmanager = {
        windows = {
            -- TODO: use winget
            xpm = {url = "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/code_1.93.1-1726079302_amd64.deb", sha256 = nil},
        },
        ubuntu = {
            xpm = {url = "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/VSCodeUserSetup-x64-1.93.1.exe", sha256 = nil},
        },
        arch = {
            -- TODO: add arch support
        },
    },
}

import("common")
import("xim.base.utils")

local vscode_file = path.filename(package.pmanager[utils.os_type()].xpm.url)

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
    local use_winget_sys = (os.getenv("USERNAME") or ""):lower() == "administrator"
    if os.host() == "windows" then
        print("[xlings]: runninng vscode installer, it may take some minutes...")
        if use_winget_sys then
            os.exec("winget install vscode --scope machine")
        else
            common.xlings_exec(vscode_file .. " /verysilent /suppressmsgboxes /mergetasks=!runcode")
        end
    elseif os.host() == "linux" then
        os.exec("sudo dpkg -i " .. vscode_file)
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
    -- TODO: uninstall vscode
end