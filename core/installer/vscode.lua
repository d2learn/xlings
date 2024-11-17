import("lib.detect.find_tool")

import("platform")
import("common")

local config = platform.get_config_info()

local vscode_url = {
    linux = "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/code_1.93.1-1726079302_amd64.deb",
    windows = "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/VSCodeUserSetup-x64-1.93.1.exe",
}

local vscode_package = {
    linux = "vscode.deb",
    windows = "vscode_setup.exe",
}

local vscode_file = path.join(config.rcachedir, vscode_package[os.host()])

function support()
    return {
        windows = true,
        linux = true,
        macosx = false
    }
end

function installed()
    return try {
        function()
            if os.host() == "windows" and (os.getenv("USERNAME") or ""):lower() == "administrator" then
                return os.isfile("C:\\Program Files\\Microsoft VS Code\\Code.exe")
            else
                os.exec("code --version")
            end
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing vscode...")

    local url = vscode_url[os.host()]
    -- only windows administrator
    local use_winget_sys = (os.getenv("USERNAME") or ""):lower() == "administrator"

    if not os.isfile(vscode_file) and not use_winget_sys then
        common.xlings_download(url, vscode_file)
    end

    return try {
        function ()
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
        end, catch {
            function (e)
                os.tryrm(vscode_file)
                return false
            end
        }
    }
end

function info()
    return {
        name     = "vscode",
        homepage = "https://code.visualstudio.com",
        author   = "https://github.com/microsoft/vscode/graphs/contributors",
        licenses = "MIT",
        github   = "https://github.com/microsoft/vscode",
        docs     = "https://code.visualstudio.com/docs",
        profile  = "Visual Studio Code",
    }
end